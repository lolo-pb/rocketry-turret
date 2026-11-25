## l0l0 logs 11/24/2026 Mk3 - Manual controls via joystick

 Made the joystick work, the actual case itself is a bit hacky 
for now but it will be remade soon. The Joystick connects via USB 
directly to the laptop connected to the GS, and is used to controll
the manual setup

Controls:
- go in and click manual setup
- connect the jpystick and aim at rocket
- Pressing the joystick in (R3 style) will arm the turret
- while tracking, double pres will go back to manual setup

Logs should be kept and well loged even while in maual setup
in case manual controll of the turret is needed.

will note details of the jpystick itself on a drive later


## l0l0 logs 11/21/2026 (00:02) Mk3 on the way

| Servo     | Pi GPIO | Physical header pin |
|  ---      |  ---:   |    ---:             |
| Azimuth   | GPIO12  | Pin 32              |
| Elevation | GPIO13  | Pin 33              |

Estos son losmpines de GPIO para los servos

la lat y long de la gs esta de momento hardcodeada 
hay un todo list en lolo-notes.md

Podemos usar el arduino de la anterior para el joystica  usb y usarlo desde la laptop.

## l0l0 logs 11/20/2026 (11:00) Mk3 on the way

Loego de deliberar y cheralar con el grupo decidimos que lo mejor
seria hacero lo con una raspi zero w con usb hat, que podria funcionar 
con usb hub tambien. 

La idea es hostear el dashboard web a travrz de un hotspot wifi,
controlar los servos con los gpio pins y conectarse con los recievers 
por usb. 

Las pantallas estan mas detalladas en el doc 
( https://docs.google.com/document/d/1F3PihDYGYD9RY9BYEDbY-V503_j1lc8zxLwpTotJwWA/edit?usp=sharing )

Falta uicamente decidir como se va a conecta el joystic. Lo ideal seria que 
sea en el dashboar, osea en alguna laptop que se conecte.

## l0l0 logs 10/19/2026 Designe reviews

 Despues de investigr un poco mas, el pasaje de usb a uart directo a los 
GPIO pins esdemasiado hacky, i no estoy seguro si anda.

 Solucion minima, usar un usb hub y connectar todo, dashboard inclusive
deberia funcionar, yo ya he probado usb hubs con raspis, habria que ver 
si la alimentacion funciona pero creo que si.

 Lo mejor por lejos seria usar los pines para rx y tx pero el telebt no tiene 
pines de salida normales, habria que tapearse en la placa entre el micro y 
el chip usb.(Mega sus) <!--preguntarle a lucas-->


## l0l0 logs 10/02/2026 Primer contacto con la torreta

Hay una plaquita que se encarga ppalmente de la alimentacion 
de los servos, switches fisicos.

Mi idea es cambiarlo y hacer algo mas asi:

        +------------+
        |         5V |----+---- Receiver 1 (VCC)
        |            |    |
        |            |    +---- Receiver 2 (VCC)
        |            |
        |        GND |----+---- Receiver 1 (GND)
        |            |    |
        |            |    +---- Receiver 2 (GND)
        |            |
        |   D2 (RX1) |--------- Receiver 1 (TX)
        |   D3 (TX1) |--------- Receiver 1 (RX)
        |   D4 (RX2) |--------- Receiver 2 (TX)
        |   D5 (TX2) |--------- Receiver 2 (RX)
        +------------+



habria que conectar los recievers al arduino, el diagrama lo hace por pines pero para eso habria que abrir el reciever.

opcion 2:
 
En vez de pines usar usb y agregarle un module de usb a la placa, medio paja pero el arduino por ahi no se la banca, por ahi una raspi pico es mejor. y seria un buen upgrade por el tema de que va a ser lo unico en la caja. 

las palanquitas no van a servir mas a 10 mtros de altura, asiq la caja se cierra la raspi se encarga de todo y habla con el dshboard


                   +-------------------------+
                   | Laptop (Power & Data)   |
                   +------------+------------+
                                |
                                | (Built-in Micro-USB / USB-C Cable)
                                v
                     +---------------------+
                     | Raspberry Pi Pico   |
                     +---------------------+
                     |                     |
                     |                     |
                     | GP18 / SPI0 SCK     |----+-------- USB Module 1 (SCK)
                     | (Pin 24)            |    |
                     |                     |    +-------- USB Module 2 (SCK)
                     |                     |
                     | GP19 / SPI0 TX/MOSI |----+-------- USB Module 1 (MOSI)
                     | (Pin 25)            |    |
                     |                     |    +-------- USB Module 2 (MOSI)
                     |                     |
                     | GP16 / SPI0 RX/MISO |----+-------- USB Module 1 (MISO)
                     | (Pin 21)            |    |
                     |                     |    +-------- USB Module 2 (MISO)
                     |                     |
                     | GP17 (Pin 22)       |------------- USB Module 1 (CS/SS)
                     | GP15 (Pin 20)       |------------- USB Module 2 (CS/SS)
                     |                     |
                     | GP20 (Pin 26)       |------------- USB Module 1 (INT)
                     | GP21 (Pin 27)       |------------- USB Module 2 (INT)
                     +---------------------+


    +----------------------+          +----------------------+
    | USB Host Module 1    |          | USB Host Module 2    |
    | (e.g. MAX3421E)      |          | (e.g. MAX3421E)      |
    +----------+-----------+          +----------+-----------+
               | (USB Socket)                    | (USB Socket)
               v                                 v
    +----------------------+          +----------------------+
    | Receiver 1 (USB Plug)|          | Receiver 2 (USB Plug)|
    +----------------------+          +----------------------+



esta medio feo pero va a ser una pico con 2 dongles usb para los recievers y usb para la laptop. 

dsps la plaquita que existe para alimentar los servos y las signal lines a la raspi.

