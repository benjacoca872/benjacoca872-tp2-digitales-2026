#include <Arduino.h>
#include <avr/interrupt.h>

uint8_t tabla[] = {0xC0, 0xF9, 0xA4, 0xB0, 0x99, 0x92, 0x83, 0xF8, 0x80, 0x98, 0xFF};

//configuro variables 
volatile uint16_t valorAdc = 0;
uint8_t numeros[4]; //lo use para cada display
uint8_t display = 0;
volatile uint32_t contador_ms = 0;
uint32_t tiempo_display = 0;

enum Estado // maquina de estados
{
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

uint16_t tiempoRegresivo = 1200;
uint16_t tiempoConfigurado = 1200;
uint32_t ultimoTick = 0;
uint32_t marcaTiempoFinal = 0;
uint16_t estadoBotonesAnterior = 0;

void config_timer0();
void config_adc();
void mostrarNumero(uint16_t numero);
void mostrarDisplay(uint8_t numDisplay, uint8_t dato);

ISR(TIMER0_COMPA_vect) { contador_ms++; }
ISR(ADC_vect)
{
  static uint32_t acum = 0;
  static uint8_t i = 0;
  acum += ADC;
  i++;
  if (i > 15)
  {
    valorAdc = acum >> 4;
    acum = 0;
    i = 0;
  }
}

void config_timer0()
{
  TCCR0A = (1 << WGM01);
  OCR0A = 62;
  TCCR0B = (1 << CS02);
  TIMSK0 = (1 << OCIE0A);
}

void config_adc()
{
  ADMUX = (1 << REFS0);
  ADCSRA = (1 << ADEN) | (1 << ADIE) | (1 << ADATE) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
  ADCSRA |= (1 << ADSC);
}

void mostrarNumero(uint16_t numero)
{
  uint8_t m = numero / 60;
  uint8_t s = numero % 60;
  numeros[0] = m / 10;
  numeros[1] = m % 10;
  numeros[2] = s / 10;
  numeros[3] = s % 10;
}

void mostrarDisplay(uint8_t numDisplay, uint8_t dato)
{
  PORTD &= (0b11110011);
  PORTB &= (0b11001111);
  PORTD &= (0b00001111);
  PORTB &= (0b11110000);
  PORTD |= (tabla[dato] << 4); //<< dezplaza los dato a todos los bits
  PORTB |= (tabla[dato] >> 4);
  switch (numDisplay)
  {
  case 0:
    PORTD |= (1 << PD2);
    break;
  case 1:
    PORTD |= (1 << PD3);
    break;
  case 2:
    PORTB |= (1 << PB4);
    break;
  case 3:
    PORTB |= (1 << PB5);
    break;
  }
}
 
int main()
{
  DDRD |= 0b11111100; //declaro estros pines como entrada
  DDRB |= 0b01111111; //delcaro estos pinhes como salida
  config_timer0(); //configuro el timer0
  config_adc(); //configuro el adc
  sei();

  while (1)
  {
    static uint16_t lecturaUltima = 0;
    static uint32_t tiempoEstabilizacion = 0;

    uint16_t botonActual = 0; //hago un promedio para cada boton con su valor 
    if (valorAdc >= 200 && valorAdc <= 270)
      botonActual = 1; // boton para star/stop//
    else if (valorAdc >= 470 && valorAdc <= 540)
      botonActual = 2; // boton para incrementar secuencia//
    else if (valorAdc >= 280 && valorAdc <= 350)
      botonActual = 3; // boton para decrementar secuencia//
    else if (valorAdc >= 670 && valorAdc <= 720)
      botonActual = 4; // Botón para cambiar de segundos a minutos//

    if (botonActual != lecturaUltima)
    {
      lecturaUltima = botonActual;
      tiempoEstabilizacion = contador_ms;
    }
    else if ((contador_ms - tiempoEstabilizacion > 50) && (botonActual != estadoBotonesAnterior))
    {
      if (botonActual != 0) //configuro cada caso de cada boton
      {
        if (estadoActual == CONFIG)
        {
          if (botonActual == 1)
          {
            estadoActual = CORRIENDO;
            ultimoTick = contador_ms;
          }
          if (botonActual == 4)
          {
            // Cambiar modo: Minutos <-> Segundos//
            if (modoActual == MINUTOS) {
              modoActual = SEGUNDOS;
            } else {
              modoActual = MINUTOS;
            }
          }
          else if (botonActual == 2) // Incrementar//
          {
            uint16_t paso;
            if (modoActual == MINUTOS) {
              paso = 60;
            } else {
              paso = 1;
            }
            
            if (tiempoRegresivo + paso <= 1200)
              tiempoRegresivo += paso;
            tiempoConfigurado = tiempoRegresivo;
          }
          else if (botonActual == 3) // Decrementar//
          {
            uint16_t paso;
            if (modoActual == MINUTOS) {
              paso = 60;
            } else {
              paso = 1;
            }
            
            if (tiempoRegresivo >= paso)
              tiempoRegresivo -= paso;
            tiempoConfigurado = tiempoRegresivo;
          }
        }
        else if (estadoActual == CORRIENDO && botonActual == 1)
        {
          estadoActual = CONFIG;
        }
      }
      estadoBotonesAnterior = botonActual;
    }

    // Lógica de estados y visualización//
    if (estadoActual == CORRIENDO)
    {
      if (contador_ms - ultimoTick >= 1000)  //lo que estoy haciendo es que aca haga la secuencia para cuando termina de contar
      {
        ultimoTick = contador_ms;
        if (tiempoRegresivo > 1)
          tiempoRegresivo--;
        else
        {
          estadoActual = FINAL;
          marcaTiempoFinal = contador_ms;
        }
      }
      mostrarNumero(tiempoRegresivo);
    }
    else if (estadoActual == FINAL)
    {
      if ((contador_ms / 500) % 2 == 0)
      {
        numeros[0] = 8;
        numeros[1] = 8;
        numeros[2] = 8;
        numeros[3] = 8;
      }
      else
      {
        numeros[0] = 10;
        numeros[1] = 10;
        numeros[2] = 10;
        numeros[3] = 10;
      }

      if (contador_ms - marcaTiempoFinal >= 10000)
      {
        estadoActual = CONFIG;
        tiempoRegresivo = tiempoConfigurado;
      }
    }
    else
    {
      mostrarNumero(tiempoRegresivo);
    }

    if ((contador_ms - tiempo_display) >= 5)  //declaro el mutiplexado q es cada 5ms
    {
      tiempo_display = contador_ms;
      PORTD &= ~(1 << PD2);
      PORTD &= ~(1 << PD3);
      PORTB &= ~(1 << PB4);
      PORTB &= ~(1 << PB5);
      if (numeros[display] < 10)
        mostrarDisplay(display, numeros[display]);
      display = (display + 1) & 0x03;
    }
  }
}