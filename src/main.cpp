#include <Arduino.h>
#include <avr/interrupt.h>

uint8_t tabla[] = {0xC0, 0xF9, 0xA4, 0xB0, 0x99, 0x92, 0x83, 0xF8, 0x80, 0x98, 0xFF};

// Variables del sistema //
volatile uint16_t valorAdc = 0;
uint8_t numeros[4]; 
uint8_t display = 0;
volatile uint32_t contador_ms = 0;
uint32_t tiempo_display = 0;

enum Estado {
  CONFIG,
  CORRIENDO,
  FINAL
};
enum ModoEdicion 
{
  MINUTOS,
  SEGUNDOS
};

Estado estadoActual = CONFIG;
ModoEdicion modoActual = MINUTOS;

uint16_t tiempoRegresivo = 1200;   // Arranca siempre en 20:00 minutos (1200 segundos) //
uint16_t tiempoConfigurado = 1200;
uint32_t ultimoTick = 0;
uint32_t marcaTiempoFinal = 0;
uint16_t estadoBotonesAnterior = 0;

#define PIN_ENC_A PC1  // CLK en Pin A1 //
#define PIN_ENC_B PC2  // DT en Pin A2 //
#define PIN_ENC_SW PC3 // SW en Pin A3 (Pulsador Encoder: START / STOP) //
uint8_t encA_ant = 0;

// CONFIGURACIÓN DE LA COLA CIRCULAR (RAM VACÍA AL ARRANCAR) //
#define CANT_MAX 10
uint16_t tiemposGuardados[CANT_MAX] = {}; // Vector completamente vacío en RAM //
uint8_t indexGuardar = 0;  // Empieza a guardar desde la posición 0 //
uint8_t indexLeer = 0;     // Puntero de lectura inicial //
uint8_t cantidadElementos = 0; // Al conectar el Arduino, hay 0 elementos guardados //

#define PIN_ALERTA PB5 

void config_timer0();
void config_adc();
void config_encoder_io();
void mostrarNumero(uint16_t numero);
void mostrarDisplay(uint8_t numDisplay, uint8_t dato);
void procesarEncoder();
void guardarEnCola(uint16_t valor);
void leerSiguienteCola();

ISR(TIMER0_COMPA_vect) { 
  contador_ms++; 
}

ISR(ADC_vect) {
  static uint32_t acum = 0;
  static uint8_t i = 0;
  acum += ADC;
  i++;
  if (i > 15) {
    valorAdc = acum >> 4;
    acum = 0;
    i = 0;
  }
}

void config_timer0() {
  TCCR0A = (1 << WGM01);
  OCR0A = 62;
  TCCR0B = (1 << CS02);
  TIMSK0 = (1 << OCIE0A);
}

void config_adc() {
  ADMUX = (1 << REFS0); 
  ADCSRA = (1 << ADEN) | (1 << ADIE) | (1 << ADATE) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
  ADCSRA |= (1 << ADSC);
}

void config_encoder_io() {
  DDRC &= ~(1 << PIN_ENC_A) & ~(1 << PIN_ENC_B) & ~(1 << PIN_ENC_SW);
  PORTC |= (1 << PIN_ENC_A) | (1 << PIN_ENC_B) | (1 << PIN_ENC_SW); 
}

void mostrarNumero(uint16_t numero) {
  uint8_t m = numero / 60;
  uint8_t s = numero % 60;
  numeros[0] = m / 10;
  numeros[1] = m % 10;
  numeros[2] = s / 10;
  numeros[3] = s % 10;
}

void mostrarDisplay(uint8_t numDisplay, uint8_t dato) {
  PORTD &= (0b11110011);
  PORTB &= (0b11001111);
  PORTD &= (0b00001111);
  PORTB &= (0b11110000);
  PORTD |= (tabla[dato] << 4); 
  PORTB |= (tabla[dato] >> 4);
  
  switch (numDisplay) {
    case 0: PORTD |= (1 << PD2); break; 
    case 1: PORTD |= (1 << PD3); break;
    case 2: PORTB |= (1 << PB4); break; 
    case 3: PORTB |= (1 << PB5); break;
  }
}

// BOTÓN 1: GUARDA EL TIEMPO ACTUAL EN LA COLA//
void guardarEnCola(uint16_t valor) {
  tiemposGuardados[indexGuardar] = valor;
  indexGuardar = (indexGuardar + 1) % CANT_MAX; // Si pasa de 10, vuelve a 0  //
  if (cantidadElementos < CANT_MAX) {
    cantidadElementos++;
  }
}

// BOTÓN 2: MUESTRA EL SIGUIENTE VALOR GUARDADO EN MEMORIA //
void leerSiguienteCola() {
  if (cantidadElementos == 0) return; // Si no guardaste nada, no hace nada //
  
  // Carga el dato guardado en la pantalla //
  tiempoRegresivo = tiemposGuardados[indexLeer];
  tiempoConfigurado = tiempoRegresivo;
  
  // Avanza el puntero de lectura de forma circular //
  indexLeer = (indexLeer + 1) % cantidadElementos; 
}

void procesarEncoder() {
  if (estadoActual != CONFIG) return; 

  uint8_t encA_actual = (PINC & (1 << PIN_ENC_A)) ? 1 : 0;
  uint8_t encB_actual = (PINC & (1 << PIN_ENC_B)) ? 1 : 0;

  // Determinar el paso según el modo activo (Minutos = 60s, Segundos = 1s) //
  uint16_t paso = (modoActual == MINUTOS) ? 60 : 1;

  if (encA_actual != encA_ant) { 
    if (encA_actual == 0) { 
      if (encB_actual == 1) {
        // Giro a la DERECHA: Incrementa tiempo //
        if (tiempoRegresivo + paso <= 1200) { // Límite máximo //
          tiempoRegresivo += paso;
        }
      } else {
        // Giro a la IZQUIERDA: Decrementa tiempo //
        if (tiempoRegresivo >= paso) {
          tiempoRegresivo -= paso;
        } else {
          tiempoRegresivo = 0; 
        }
      }
      tiempoConfigurado = tiempoRegresivo; 
    }
  }
  encA_ant = encA_actual; 
}

int main() {
  DDRD |= 0b11111100; 
  DDRB |= 0b01111111; 
  
  config_encoder_io();
  config_timer0(); 
  config_adc(); 
  sei();

  encA_ant = (PINC & (1 << PIN_ENC_A)) ? 1 : 0;

  while (1) {
    static uint16_t lecturaUltima = 0;
    static uint32_t tiempoEstabilizacion = 0;

    procesarEncoder();

    // LECTURA DE BOTONES //
    uint16_t botonActual = 0; 
    
    // Pulsador físico en el eje del Encoder (Pin A3 digital) //
    if ((PINC & (1 << PIN_ENC_SW)) == 0) {
      botonActual = 1; // START / STOP //
    } 
    // Botonera analógica en Pin A0
    else if (valorAdc >= 200 && valorAdc <= 270) {
      botonActual = 2; // BOTÓN 1: Almacenar valor del display en RAM //
    } 
    else if (valorAdc >= 470 && valorAdc <= 540) {
      botonActual = 3; // BOTÓN 2: Mostrar valores guardados en display //
    }
    else if (valorAdc >= 280 && valorAdc <= 350) {
      botonActual = 4; // BOTÓN 3: Cambiar segundo / minuto //
    }

    // Anti-rebote //
    if (botonActual != lecturaUltima) {
      lecturaUltima = botonActual;
      tiempoEstabilizacion = contador_ms;
    } 
    else if ((contador_ms - tiempoEstabilizacion > 50) && (botonActual != estadoBotonesAnterior)) {
      if (botonActual != 0) {
        
        // CORRECCIÓN: Separamos las acciones de los botones 1 y 4 independientes uno del otro
        if (botonActual == 1) // START / STOP //
        {
          if (estadoActual == CONFIG && tiempoRegresivo > 0) {
            estadoActual = CORRIENDO;
            ultimoTick = contador_ms;
          } else if (estadoActual == CORRIENDO) {
            estadoActual = CONFIG;
          }
        }
        else if (botonActual == 4) // CAMBIAR MODO (MINUTOS / SEGUNDOS) //
        {
          if (modoActual == MINUTOS) {
            modoActual = SEGUNDOS;
          } else {
            modoActual = MINUTOS;
          }
        }
        else if (estadoActual == CONFIG) { // Acciones de memoria (solo en pausa) //
          if (botonActual == 2) {
            guardarEnCola(tiempoRegresivo); 
          } 
          else if (botonActual == 3) {
            leerSiguienteCola(); 
          }
        }
      }
      estadoBotonesAnterior = botonActual;
    }

    // Máquina de estados //
    if (estadoActual == CORRIENDO) {
      if (contador_ms - ultimoTick >= 1000) {
        ultimoTick = contador_ms;
        if (tiempoRegresivo > 0)
          tiempoRegresivo--;
        else {
          estadoActual = FINAL;
          marcaTiempoFinal = contador_ms;
        }
      }
      mostrarNumero(tiempoRegresivo);
    } 
    else if (estadoActual == FINAL) {
      if ((contador_ms / 500) % 2 == 0) {
        numeros[0] = 8; numeros[1] = 8; numeros[2] = 8; numeros[3] = 8;
      } else {
        numeros[0] = 10; numeros[1] = 10; numeros[2] = 10; numeros[3] = 10; 
      }

      if (contador_ms - marcaTiempoFinal >= 10000) {
        estadoActual = CONFIG;
        tiempoRegresivo = tiempoConfigurado; 
      }
    } 
    else {
      mostrarNumero(tiempoRegresivo);
    }

    // Multiplexación de los displays //
    if (contador_ms - tiempo_display >= 4) {
      tiempo_display = contador_ms;
      mostrarDisplay(display, numeros[display]);
      display = (display + 1) % 4;
    }
  }
}
