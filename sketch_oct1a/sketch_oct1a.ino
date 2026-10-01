#include <Servo.h>

Servo handServo;

#define SERVO_PIN D4

void moveServoSmooth(int fromAngle, int toAngle)
{
  if (fromAngle < toAngle)
  {
    for (int angle = fromAngle; angle <= toAngle; angle++)
    {
      handServo.write(angle);
      delay(15);
    }
  }
  else
  {
    for (int angle = fromAngle; angle >= toAngle; angle--)
    {
      handServo.write(angle);
      delay(15);
    }
  }
}

void setup()
{
  handServo.attach(SERVO_PIN);

  // FIRST POSITION = 0°
  handServo.write(0);
  delay(1000);
}

void loop()
{
  // 0° → 90°
  moveServoSmooth(0, 90);

  // Hold at 90° for 3 seconds
  delay(3000);

  // 90° → 0°
  moveServoSmooth(90, 0);

  // Hold at 0° for 3 seconds
  delay(3000);
}