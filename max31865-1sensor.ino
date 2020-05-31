#include <SPI.h>
#include <Adafruit_MAX31865.h>

// use hardware SPI, just pass in the CS pin
Adafruit_MAX31865 thermo1 = Adafruit_MAX31865(10, 5, 6, 7); // Adafruit_MAX31865(10);
Adafruit_MAX31865 thermo2 = Adafruit_MAX31865(9, 5, 6, 7); // Adafruit_MAX31865(9);


// The value of the Rref resistor. Use 430.0 for PT100 and 4300.0 for PT1000
#define RREF      4300.0
// The 'nominal' 0-degrees-C resistance of the sensor
// 100.0 for PT100, 1000.0 for PT1000
#define RNOMINAL  1000.0

// Write info to the console
const boolean debug = false;

// Resister in series to move the range up
// with 890Ohm we can get from ~ -15°C (941Ohm) to 275°C (1890Ohm)
// Adjust the offset if the results on the heating system are of
const int rOffset = 905;

// AD8400 1k DigiPot has a non linear output ?!!?
// 5 Samples @64 steps are used to reduce error
const int rStep0 (51 + rOffset);
const int rStep64 (331 + rOffset);
const int rStep128 (607 + rOffset);
const int rStep192 (878 + rOffset);
const int rStep255 (1143 + rOffset);

byte address = 0x00;
int outputCs = 8;
int lastRValue = -1;

void setup()
{
  if ( debug )
  {
    Serial.begin(115200);
  }

  pinMode (outputCs, OUTPUT);

  thermo1.begin(MAX31865_2WIRE);  // set to 2WIRE or 4WIRE as necessary
  thermo2.begin(MAX31865_2WIRE);  // set to 2WIRE or 4WIRE as necessary

  // Onboard LED is same pin as SPI, and I don't have an LED about...
  // pinMode(LED_BUILTIN, OUTPUT);

  SPI.begin();
}


void loop()
{
  float tOut = mergeTempretures( thermo1, thermo2 );
  if ( debug )
  {
    Serial.print(tOut);
    Serial.println("°C");
  }

  delay(2000); // 2s
}

float getReistance(Adafruit_MAX31865 thermo)
{
  uint16_t rtd = thermo.readRTD();

  float ratio = rtd;
  return RREF * ratio / 32768;
}

/**
   Merges two sensor tempretures together for solar collector
*/
float mergeTempretures(Adafruit_MAX31865 thermo1, Adafruit_MAX31865 thermo2)
{
  // T1 having problems use T2
  if ( checkThermostatError( thermo1 ) )
  {
    setPotValue( getReistance(thermo1) );
    return thermo2.temperature(RNOMINAL, RREF);
  }

  // T2 having problems use T1
  if ( checkThermostatError( thermo2 ) )
  {
    setPotValue( getReistance(thermo2) );
    return thermo1.temperature(RNOMINAL, RREF);
  }

  float t1 = thermo1.temperature(RNOMINAL, RREF);
  float t2 = thermo2.temperature(RNOMINAL, RREF);

  if ( debug )
  {
      Serial.print("");
      Serial.print(t1);
      Serial.print(" t1 / ");
      Serial.print(t2);
      Serial.print(" t2");
      Serial.println("");
  }

  // Case 1: COLD
  // Return the coldest for frost protection
  if ( t1 <= 0 || t2 <= 0 )
  {
    if ( t1 < t2 )
    {
      if ( debug )
      {
        Serial.println("Using thermometer 1");
      }

      setPotValue( getReistance(thermo1) );
      return t1;
    }

    if ( debug )
    {
      Serial.println("Using thermometer 2");
    }

    setPotValue( getReistance(thermo2) );
    return t2;
  }

  float tMax = 0;
  // Case 2: HOT
  // Return the hostest to get the water circulating
  if ( t1 > t2 )
  {
    tMax = t1;

    if ( debug )
    {
      Serial.println("Using thermometer 1");
    }
  }
  else
  {
    tMax = t2;
    if ( debug )
    {
      Serial.println("Using thermometer 2");
    }
  }

  // Over 50°c don't mess with the results
  if ( tMax > 50 )
  {
    setPotValue( getReistance( t1 > t2 ? thermo1 : thermo2 ) );
    return tMax;
  }

  // We'll avrage the tempreture to stop avoid
  // the system constantly stopping and starting
  float r1 =  getReistance( thermo1 );
  float r2 =  getReistance( thermo2 );
  // This works because the resistance of the PT100(0) is linear!
  setPotValue( (r1 + r2) / 2 );
  return ( (t1 + t2) / 2);
}

boolean checkThermostatError( Adafruit_MAX31865 thermo )
{
  // Check and print any faults
  uint8_t fault = thermo.readFault();
  if ( fault == 0 )
  {
    return false;
  }

  if ( debug )
  {
    Serial.print("Fault 0x"); Serial.println(fault, HEX);
    if (fault & MAX31865_FAULT_HIGHTHRESH) {
      Serial.println("RTD High Threshold");
    }
    if (fault & MAX31865_FAULT_LOWTHRESH) {
      Serial.println("RTD Low Threshold");
    }
    if (fault & MAX31865_FAULT_REFINLOW) {
      Serial.println("REFIN- > 0.85 x Bias");
    }
    if (fault & MAX31865_FAULT_REFINHIGH) {
      Serial.println("REFIN- < 0.85 x Bias - FORCE- open");
    }
    if (fault & MAX31865_FAULT_RTDINLOW) {
      Serial.println("RTDIN- < 0.85 x Bias - FORCE- open");
    }
    if (fault & MAX31865_FAULT_OVUV) {
      Serial.println("Under/Over voltage");
    }
  }

  thermo.clearFault();
  /*
    // Error Blink the LED
    for ( int i = 0; i < 5; i++ )
    {
      Serial.println("Blink");
      digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
      delay(500);
      digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
      delay(500);
    }
  */
  return true;
}

void setPotValue( float resistance )
{
  if ( debug )
  {
    Serial.print(resistance);
    Serial.println(" Ohm");
  }

  int value = 0;
  if ( resistance > rStep255 ) // Edge cases upper limit
  {
    value = 255;
  }
  else if ( resistance < rStep0 )  // Edge case lower limit
  {
    value = 0;
  }
  else if ( resistance < rStep64 )
  {
    value = resistanceToSteps(resistance, rStep0, rStep64);
  }
  else if ( resistance < rStep128 )
  {
    value = 64 + resistanceToSteps(resistance, rStep64, rStep128);
  }
  else if ( resistance < rStep192 )
  {
    value = 128 + resistanceToSteps(resistance, rStep128, rStep192);
  }
  else
  {
    value = 192 + resistanceToSteps(resistance, rStep192, rStep255);
  }

  if ( value == lastRValue )
  {
    return;
  }

  lastRValue = value;

  if ( debug )
  {
    Serial.print(value);
    Serial.println(" value");
  }

  digitalWrite(outputCs, LOW);
  SPI.transfer(address);
  SPI.transfer(value);
  digitalWrite(outputCs, HIGH);
}

int resistanceToSteps( int resistance, int _rStep0, int _rStep1 )
{
  float ohmsPerStep = (_rStep1 - _rStep0) / 64.0;
  return floor( ( resistance - _rStep0 ) / ohmsPerStep );
}
