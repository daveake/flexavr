unsigned long NextLEDs=0;

void SetupLEDs(void)
{
  pinMode(LED_WARN, OUTPUT);
  digitalWrite(LED_WARN, 1);

  pinMode(LED_OK, OUTPUT);
  digitalWrite(LED_OK, 0);
}

void ControlLEDs(int LEDOK, int LEDWarn)
{
  digitalWrite(LED_OK, LEDOK);

  digitalWrite(LED_WARN, LEDWarn);
}

void CheckLEDs(void)
{
  if (millis() >= NextLEDs)
  {
    static byte Flash=0;
    
    // This would normally be the only LED for status (i.e. no OK or WARN LEDs)
    if (GPS.Altitude > 1000)
    {
      // All off
      ControlLEDs(0,0);
    }
    else if ((GPS.FixType == 3) && (GPS.Satellites >= 4))
    {
      ControlLEDs(Flash, 0);
    }
    else
    {
      ControlLEDs(0, Flash);
    }       
    
    NextLEDs = millis() + 500L;
    Flash = 1-Flash;
  }
}
