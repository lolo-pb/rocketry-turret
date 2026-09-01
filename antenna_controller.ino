#include <Servo.h>

const int PIN_SERVO_AZIMUT = 10;
const int PIN_SERVO_ELEVACION = 11;

Servo servoAzimut;
Servo servoElevacion;

// ==============================
// OFFSET FRONTAL — posición física donde el servo apunta al cohete
// con yaw=0 y tilt=0 (cohete enfrente de la ground station)
const int OFFSET_AZ = 110;
const int OFFSET_EL = 105;

// DIRECCIÓN — flipear a -1 si el servo se mueve al revés (ajustar probando)
const int DIR_AZ =
    1; // yaw positivo -> servo aumenta. Cambiar a -1 si va al reves
const int DIR_EL =
    1; // tilt positivo -> servo aumenta. Cambiar a -1 si va al reves

// LÍMITES MECÁNICOS DE SEGURIDAD — ajustar al rango real de tus servos
const int AZ_MIN = 0;
const int AZ_MAX = 270;
const int EL_MIN = 0;
const int EL_MAX = 270;
// ==============================

const int POS_STEP = 1; // Note, this needs to be 1 or else it can overshoot

int posActualAz = OFFSET_AZ;
int posActualEl = OFFSET_EL;

const int PASO_MS = 40; // ms entre cada grado (mas alto = mas lento)

void setup() {
  Serial.begin(115200);
  servoAzimut.attach(PIN_SERVO_AZIMUT);
  servoElevacion.attach(PIN_SERVO_ELEVACION);

  // Arranca en la posicion frontal de calibracion
  servoAzimut.write(posActualAz);
  servoElevacion.write(posActualEl);
  delay(100);

  Serial.println("LISTO. Enviar: Y<yaw> T<tilt>  (ej: Y15 T30)");
}

void loop() {
  static int loops = 0;
  static int targetYaw = posActualAz;
  static int targetTilt = posActualEl;

  if (Serial.available() > 0) {
    char tipo = Serial.read();
    if (tipo == 'Y') {
      float yaw = Serial.parseFloat();
      targetYaw = OFFSET_AZ + DIR_AZ * (int)round(yaw);
    } else if (tipo == 'T') {
      float tilt = Serial.parseFloat();
      targetTilt = OFFSET_EL + DIR_EL * (int)round(tilt);
    }
    // Cualquier otro caracter (espacios, \n, \r) se ignora
  }
  approach(targetYaw, targetTilt);
  delay(PASO_MS);

  if (loops++ % 10 == 0) // TODO: esto puede ser mas grande (Probar)
    reportar();
}
// approaches target without blocking
void approach(int targetYaw, int targetTilt) {
  approachYaw(targetYaw);
  approachTilt(targetTilt);
}
void approachYaw(int t) {
  t = constrain(t, AZ_MIN, AZ_MAX);
  if (posActualAz != t) {
    posActualAz += (t > posActualAz) ? POS_STEP : -POS_STEP;
    servoAzimut.write(posActualAz);
  }
}
void approachTilt(int t) {
  t = constrain(t, EL_MIN, EL_MAX);
  if (posActualEl != t) {
    posActualEl += (t > posActualEl) ? POS_STEP : -POS_STEP;
    servoElevacion.write(posActualEl);
  }
}

void reportar() {
  Serial.print("Az: ");
  Serial.print(posActualAz);
  Serial.print(" | El: ");
  Serial.println(posActualEl);
}