#include <Servo.h>

const int PIN_SERVO_AZIMUT = 10;
const int PIN_SERVO_ELEVACION = 11;

Servo servoAzimut;
Servo servoElevacion;

// ==============================
// OFFSET FRONTAL — posición física donde el servo apunta al cohete
// con azimut=0 y elevation=0 (cohete enfrente de la ground station)
const int OFFSET_AZ = 110;
const int OFFSET_EL = 105;

// DIRECCIÓN — flipear a -1 si el servo se mueve al revés (ajustar probando)
const int DIR_AZ = 1;
// azimut positivo -> servo aumenta. Cambiar a -1 si va al reves
const int DIR_EL = 1;
// elevation positivo -> servo aumenta. Cambiar a -1 si va al reves

// LÍMITES MECÁNICOS DE SEGURIDAD — ajustar al rango real de tus servos
const int AZ_MIN = 0, AZ_MAX = 270;
const int EL_MIN = 0, EL_MAX = 270;
// ==============================

int posActualAz = OFFSET_AZ;
int posActualEl = OFFSET_EL;

const int STEP_MS = 40; // ms entre cada loop (mas alto = mas lento)
const int STEP_POS = 1; // Movimiento por loop del servo
//                         Note, this needs to be 1 or else it can overshoot

void setup() {
  Serial.begin(115200);
  servoAzimut.attach(PIN_SERVO_AZIMUT);
  servoElevacion.attach(PIN_SERVO_ELEVACION);

  // Arranca en la posicion frontal de calibracion
  servoAzimut.write(posActualAz);
  servoElevacion.write(posActualEl);
  delay(100);

  Serial.println("LISTO. Enviar: A<azimut> E<elevation>  (ej: A15 E30)");
}

void loop() {
  static int loops = 0;
  static int targetAzimut = posActualAz;
  static int targetElevation = posActualEl;

  if (Serial.available() > 0) {
    char tipo = Serial.read();
    if (tipo == 'A') {
      float azimut = Serial.parseFloat();
      targetAzimut = OFFSET_AZ + DIR_AZ * (int)round(azimut);
    } else if (tipo == 'E') {
      float elevation = Serial.parseFloat();
      targetElevation = OFFSET_EL + DIR_EL * (int)round(elevation);
    }
    // Cualquier otro caracter (espacios, \n, \r) se ignora
  }
  approach(targetAzimut, targetElevation);
  delay(STEP_MS);

  if (loops++ % 10 == 0) // TODO: esto puede ser mas grande (Probar)
    reportar();
}
// approaches target without blocking
void approach(int targetAz, int targetEl) {
  approachAzimut(targetAz);
  approachElevation(targetEl);
}
void approachAzimut(int t) {
  t = constrain(t, AZ_MIN, AZ_MAX);
  if (posActualAz != t) {
    posActualAz += (t > posActualAz) ? STEP_POS : -STEP_POS;
    servoAzimut.write(posActualAz);
  }
}
void approachElevation(int t) {
  t = constrain(t, EL_MIN, EL_MAX);
  if (posActualEl != t) {
    posActualEl += (t > posActualEl) ? STEP_POS : -STEP_POS;
    servoElevacion.write(posActualEl);
  }
}

void reportar() {
  Serial.print("Az: ");
  Serial.print(posActualAz);
  Serial.print(" | El: ");
  Serial.println(posActualEl);
}
