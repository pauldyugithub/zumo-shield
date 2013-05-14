#include <ZumoMotors.h>
#include <Pushbutton.h>
#include <Wire.h>
#include <LSM303.h>

/* This example uses the magnetometer in the Zumo Shield's onboard
 * LSM303DLHC to help the Zumo make precise 90-degree turns and drive
 * in squares. It uses the ZumoMotors, Pushbutton, and LSM303
 * (compass) libraries. The LSM303 library is not included in the Zumo
 * Shield libraries; it can be downloaded from GitHub at:
 *
 *   https://github.com/pololu/LSM303
 *
 * This program first calibrates the compass to account for offsets in
 *  its output. Calibration is accomplished in setup().
 *
 * In loop(), The driving angle then changes its offset by 90 degrees
 * from the heading every second. Essentially, this navigates the
 * Zumo to drive in square patterns.
 *
 * It is important to note that stray magnetic fields from electric
 * current (including from the Zumo's own motors) and the environment
 * (for example, steel rebar in a concrete floor) might adversely
 * affect readings from the LSM303 compass and make them less
 * reliable.
 */

#define SPEED 200 // Maximum motor speed when turning or going straight

#define CALIBRATION_SAMPLES 70  // Number of compass readings to take when calibrating
#define CRA_REG_M_220HZ 0x1C    // CRA_REG_M value for magnetometer 220 Hz update rate

// Allowed deviation (in degrees) relative to target angle that must be achieved before driving straight
#define DEVIATION_THRESHOLD 5

ZumoMotors motors;
Pushbutton button(ZUMO_BUTTON);
LSM303 compass;

// Setup will calibrate our compass by finding maximum/minimum magnetic readings
void setup()
{
  // The highest possible magnetic value to read in any direction is 2047
  // The lowest possible magnetic value to read in any direction is -2047
  LSM303::vector running_min = {2047, 2047, 2047}, running_max = {-2048, -2048, -2048};
  unsigned char index;

  Serial.begin(9600);

  // Initiate the Wire library and join the I2C bus as a master
  Wire.begin();

  // Initiate LSM303
  compass.init();

  // Enables accelerometer and magnetometer
  compass.enableDefault();

  compass.setMagGain(compass.magGain_25);                  // +/- 2.5 gauss sensitivity to hopefully avoid overflow problems
  compass.writeMagReg(LSM303_CRA_REG_M, CRA_REG_M_220HZ);  // 220 Hz compass update rate

  float min_x_avg[CALIBRATION_SAMPLES];
  float min_y_avg[CALIBRATION_SAMPLES];
  float max_x_avg[CALIBRATION_SAMPLES];
  float max_y_avg[CALIBRATION_SAMPLES];

  button.waitForButton();

  Serial.println("starting calibration");

  // To calibrate the magnetometer, the Zumo spins to find the max/min
  // magnetic vectors. This information is used to correct for offsets
  // in the magnetometer data.
  motors.setLeftSpeed(SPEED);
  motors.setRightSpeed(-SPEED);

  for(index = 0; index < CALIBRATION_SAMPLES; index ++)
  {
    // Take a reading of the magnetic vector and store it in compass.m
    compass.read();

    running_min.x = min(running_min.x, compass.m.x);
    running_min.y = min(running_min.y, compass.m.y);

    running_max.x = max(running_max.x, compass.m.x);
    running_max.y = max(running_max.y, compass.m.y);

    Serial.println(index);

    delay(50);
  }

  motors.setLeftSpeed(0);
  motors.setRightSpeed(0);

  Serial.print("max.x   ");
  Serial.print(running_max.x);
  Serial.println();
  Serial.print("max.y   ");
  Serial.print(running_max.y);
  Serial.println();
  Serial.print("min.x   ");
  Serial.print(running_min.x);
  Serial.println();
  Serial.print("min.y   ");
  Serial.print(running_min.y);
  Serial.println();

  // Set calibrated values to compass.m_max and compass.m_min
  compass.m_max.x = running_max.x;
  compass.m_max.y = running_max.y;
  compass.m_min.x = running_min.x;
  compass.m_min.y = running_min.y;

  button.waitForButton();
}

void loop()
{
  int heading, relative_heading, speed;
  static int target_heading = averageHeading();

  // Heading is given in degrees away from the magnetic vector, increasing clockwise
  heading = averageHeading();

  // This gives us the relative heading with respect to the target angle
  relative_heading = relativeHeading(heading, target_heading);

  Serial.print("Target heading: ");
  Serial.print(target_heading);
  Serial.print("    Actual heading: ");
  Serial.print(heading);
  Serial.print("    Difference: ");
  Serial.print(relative_heading);

  // If the Zumo has turned to the direction it wants to be pointing, go straight and then do another turn
  if(abs(relative_heading) < DEVIATION_THRESHOLD)
  {
    motors.setSpeeds(SPEED, SPEED);

    Serial.print("   Straight");

    delay(1000);

    // Turn off motors and wait a short time to reduce interference from motors
    motors.setSpeeds(0, 0);
    delay(100);

    // Turn 90 degrees relative to the direction we are pointing.
    // This will help account for variable magnetic field, as opposed
    // to using fixed increments of 90 degrees from the initial
    // heading (which might have been measured in a different magnetic
    // field than the one the Zumo is experiencing now).
    target_heading = (averageHeading() + 90) % 360;
  }
  else
  {
    // To avoid overshooting, the closer the Zumo gets to the target 
    // heading, the slower it should turn. Set the motor speeds to a
    // minimum of +/-100 plus an additional amount based on the
    // heading difference.
  
    speed = SPEED*relative_heading/180;
    
    if (speed < 0)
      speed -= 100;
    else
      speed += 100;
    
    motors.setSpeeds(speed, -speed);

    Serial.print("   Turn");
  }
  Serial.println();
}

// The closer we get to our driving angle, the slower we will turn.
// We do not want to overshoot our direction.
// turnLeft() takes the ideal speed and refactors it accordingly to how close
// the zumo is pointing towards the direction we want to drive.

// Converts x and y components of a vector to a heading in degrees.
// This function is used instead of LSM303::heading() because we don't
// want the acceleration of the Zumo to factor spuriously into the
// tilt compensation that LSM303::heading() performs. This calculation
// assumes that the Zumo is always level.
int heading(LSM303::vector v)
{
  float x_scaled =  2.0*(float)(v.x - compass.m_min.x) / ( compass.m_max.x - compass.m_min.x) - 1.0;
  float y_scaled =  2.0*(float)(v.y - compass.m_min.y) / (compass.m_max.y - compass.m_min.y) - 1.0;

  int angle = round(atan2(y_scaled, x_scaled)*180 / M_PI);
  if (angle < 0)
    angle += 360;
  return angle;
}

// Yields the angle difference in degrees between two headings
int relativeHeading(int heading_from, int heading_to)
{
  int relative_heading = heading_to - heading_from;
  
  // constrain to -180 to 180 degree range
  if (relative_heading > 180)
    relative_heading -= 360;
  if (relative_heading < -180)
    relative_heading += 360;
    
  return relative_heading;
}

// Average 10 vectors to get a better measurement and help smooth out
// the motors' magnetic interference.
int averageHeading()
{
  LSM303::vector avg = {0, 0, 0};

  for(int i = 0; i < 10; i ++)
  {
    compass.read();
    avg.x += compass.m.x;
    avg.y += compass.m.y;
  }
  avg.x /= 10.0;
  avg.y /= 10.0;

  // avg is the average measure of the magnetic vector.
  return heading(avg);
}