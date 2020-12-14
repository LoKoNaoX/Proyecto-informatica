#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "DHTesp.h"
// Librería necesaria para la serialización/deserialización de msgs en JSON
#include <ArduinoJson.h>
// Librería necesaria para realizar las actualizaciones de software de forma inalámbrica
#include <ESP8266httpUpdate.h>
#include "Button2.h";

// URL del servidor de actualizaciones: http://localhost:1880/esp8266-ota

// Datos para actualización   >>>> SUSTITUIR IP <<<<<
#define HTTP_OTA_ADDRESS      F("192.168.1.113")       // Address of OTA update server
#define HTTP_OTA_PATH         F("/esp8266-ota/update") // Path to update firmware
#define HTTP_OTA_PORT         1880                     // Port of update server                                                      
#define HTTP_OTA_VERSION      String(__FILE__).substring(String(__FILE__).lastIndexOf('\\')+1) + ".nodemcu" // Name of firmware

#define BUTTON_PIN  0
Button2 button = Button2(BUTTON_PIN);

// Funciones necesarias para la actualización OTA
void progreso_OTA(int,int);
void final_OTA();
void inicio_OTA();
void error_OTA(int);

// Línea necesaria para leer el voltaje Vcc observado en la entrada de alimentación
ADC_MODE(ADC_VCC);
// Variable manejadora del sensor DHT
DHTesp dht;

const char* ssid = "No_robes_vecino";
const char* password = "silyconhouse2021";

const char* mqtt_server = "iot.ac.uma.es";
const char* user="infind";
const char* pass="zancudo";

// Variable en la que se almacenará la intensidad deseada del LED (0-100) que recibamos a 
// través del topic infind/GRUPO8/led/cmd. Mientras no recibamos nada valdrá 0 (LED apagado)
int level=0;

//Variable que indica la frecuencia con la que se actualizan los datos
int Temp = 300000; //ms

//Variables utiles para la configuracion del led
int PWM;
int PWMold=0;
int frecled=10;

// String de hasta 512 caracteres en el que almacenaremos la estructura JSON con los datos
// (sensores, Vcc, uptime)... a publicar
char sdatos[512];
// String en el que almacenaremos la estructura JSON con el nivel de intensidad leído del
// LED para publicarlo
char sledstatus[256];
// String de en el que almacenaremos la estructura JSON con el estado de la conexión para pub
char sconexion[256];

//Variables necesarias para controlar el led a traves del boton flash
int boton_flash=0;       // GPIO0 = boton flash
int estado_int=HIGH;     // por defecto HIGH (PULLUP). Cuando se pulsa se pone a LOW

volatile unsigned long ultima_int = 0;
volatile unsigned long ahora=0;
volatile bool inte=false;
volatile int lectura;
bool led_control=true;
int PWMflash;


//Variables para comprueboactualizacion
unsigned long frecActualizacion=0; //frecuencia con la que comprueba si hay una actualizacion
unsigned long lastActualizacion=0;
bool actualizaMQTT=false;

bool LedDigital=false;

// Creamos una instancia de cliente MQTT de tipo WiFi asociada a client
WiFiClient espClient;
PubSubClient client(espClient);
// Hora (en ms) en la que publicamos el último msg
unsigned long lastMsg = 0;
unsigned long lastPWM = 0;

//-------------- setup_wifi --------------//

void setup_wifi() 
{
  delay(10);
  
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

//-------------- callback --------------//

void callback(char* topic, byte* payload, unsigned int length) 
{
  // Reservo un espacio en memoria de longitud length+1 para copiar el msg recibido
  char *mensaje=(char *)malloc(length+1);
  // Copio el msg recibido (payload) en el espacio reservado (mensaje)
  strncpy(mensaje,(char*)payload,length);

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");
  // Mostramos el payload recibido caracter a caracter
  for (int i = 0; i < length; i++) 
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Comprobamos que el msg se ha recibido por el topic infind/GRUPO8/config
    
    if(strcmp(topic,"infind/GRUPO8/config")==0)
  {
    // Creamos la estructura JSON root en la que deserializaremos el msg
    StaticJsonDocument<512> root; 
    // Deserializamos el mensaje en root. Además, se guarda si ha habido un error
    DeserializationError error = deserializeJson(root, mensaje);

    // Si hubo un error, informo por pantalla de cuál es
    if (error) 
    {
      Serial.print("Error, deserializeJson() failed: ");
      Serial.println(error.c_str());
    }
    // Si no hubo error,
    else{

       //comprobamos si el objeto son los datos de la frecuencia de actualizacion
       // Compruebo si la estruct JSON root, contiene la clave (campo) que estamos buscando
       if(root.containsKey("frecuencia"))
       {
         // Comprobamos que el valor de level está dentro del rango para un correcto funcionamiento
         if (root["frecuencia"]>=2)
         {
           // Guardamos en la vble level el valor correspondiente a la clave "frecuencia"
           Temp = root["frecuencia"];
           Temp = Temp*1000;
           // Indicamos por pantalla que hemos sido capaces de extraer el valor frecuencia del msg JSON
           Serial.print("Mensaje OK, frecuencia de envio de datos = ");
           Serial.println(Temp);
         }
         else{
          Serial.println("El valor de \"frecuencia\" recibido es un numero negativo");
          }
          
        }
        // Si no existe ninguna clave en root que se llame frecuencia, lo indicamos por consola
        else
        {
          Serial.print("Error : ");
          Serial.println("\"frecuencia\" key not found in JSON");
        } 

      
       //comprobamos si el objeto son los datos de la frecuencia del led
       
       // Compruebo si la estruct JSON root, contiene la clave (campo) que estamos buscando
       if(root.containsKey("frecled"))
       {
         // Comprobamos que el valor de level está dentro del rango para un correcto funcionamiento
         if (root["frecled"]>=0)
         {
           // Guardamos en la vble level el valor correspondiente a la clave "frecuencia"
           frecled = root["frecled"];
           // Indicamos por pantalla que hemos sido capaces de extraer el valor frecuencia del msg JSON
           Serial.print("Mensaje OK, frecuencia del led = ");
           Serial.println(frecled);
         }
         else{
          Serial.println("El valor de \"frecuencia\" recibido es un numero negativo");
          }
          
        }
        // Si no existe ninguna clave en root que se llame frecuencia, lo indicamos por consola
        else
        {
          Serial.print("Error : ");
          Serial.println("\"frecuencia\" key not found in JSON");
        } 
      
      //comprobamos si el objeto son los datos del LED
      
      // Compruebo si la estruct JSON root, contiene la clave (campo) que estamos buscando (led)
      if(root.containsKey("level"))
      {
        // Comprobamos que el valor de level está dentro del rango para un correcto funcionamiento
        if (root["level"]>=0 and root["level"]<=100)
        {
          // Guardamos en la vble level el valor correspondiente a la clave "level"
          level = root["level"];
          // Indicamos por pantalla que hemos sido capaces de extraer el valor level del msg JSON
          Serial.print("Mensaje OK, level = ");
          Serial.println(level);
          LED();
        }
        else{
          Serial.println("El valor de \"level\" recibido está fuera del rango 0-100");
        }
      }
      // Si no existe ninguna clave en root que se llame level, lo indicamos por consola
      else
      {
        Serial.print("Error : ");
        Serial.println("\"level\" key not found in JSON");
      }
      if(root.containsKey("actualiza"))
       {
           // Guardamos en la vble Actualizacion el valor correspondiente a la clave "frecuencia"
           actualizaMQTT = root["actualiza"];
           // Indicamos por pantalla que hemos sido capaces de extraer el valor frecuencia del msg JSON
           Serial.print("Mensaje OK, actualiza = ");
           Serial.println(actualizaMQTT);
         }
        // Si no existe ninguna clave en root que se llame frecuencia, lo indicamos por consola
        else
        {
          Serial.print("Error : ");
          Serial.println("\"Actualizacion\" key not found in JSON");
        }   
    }
          // Compruebo si la estruct JSON root, contiene la clave (campo) que estamos buscando (led)
      if(root.containsKey("frecActualizacion"))
      {
        // Comprobamos que el valor de level está dentro del rango para un correcto funcionamiento
        if (root["frecActualizacion"]>=0)
        {
          // Guardamos en la vble level el valor correspondiente a la clave "level"
          frecActualizacion = root["frecActualizacion"];
          frecActualizacion=frecActualizacion*60000;//Para pasarlo a minutos 
          // Indicamos por pantalla que hemos sido capaces de extraer el valor level del msg JSON
          Serial.print("Mensaje OK, frecActualizacion = ");
          Serial.println(frecActualizacion);
        }
        else{
          Serial.println("El valor de \"frecActualizacion\" recibido está fuera del rango");
        }
      }
      // Si no existe ninguna clave en root que se llame frecActualizacion, lo indicamos por consola
      else
      {
        Serial.print("Error : ");
        Serial.println("\"frecActualizacion\" key not found in JSON");
      }

      //Comprobamos topic de led Digital
      if(root.containsKey("LedDigital"))
       {
           // Guardamos en la vble Actualizacion el valor correspondiente a la clave "frecuencia"
           LedDigital = root["LedDigital"];
           // Indicamos por pantalla que hemos sido capaces de extraer el valor frecuencia del msg JSON
           Serial.print("Mensaje OK, Led digital = ");
           Serial.println(LedDigital);
           LED();
         }
        // Si no existe ninguna clave en root que se llame frecuencia, lo indicamos por consola
        else
        {
          Serial.print("Error : ");
          Serial.println("\"LedDigital\" key not found in JSON");
        } 
  }
  
}

//-------------- reconnect --------------//

void reconnect() 
{
  while (!client.connected()) 
  { 
    Serial.print("Attempting MQTT connection...");
    
    // En este caso, el clientId no es aleatorio, pero sigue siendo único porque contiene
    // el ChipId de nuestra placa. GetChipId devuelve un entero -> lo pasamos a string
    String clientId = "ESP8266Client-";
    clientId += String(ESP.getChipId()) ;

    // Formateamos el msg LWT en la estructura JSON conexion, que enviaremos al broker
    // al conectarnos (en la función conect)
    StaticJsonDocument<256> conexion;
    // Creamos la clave "online" y le asociamos el valor false. De esta forma, al ser un msg
    // LWT,cuando nos desconectemos abruptamente, se informará a los clientes que estén 
    // suscritos al topic conexión de que el dispositivo no está conectado
    conexion["online"] = false;
    // Serializamos la estructura JSON conexion en el string sconexion y ya esta listo
    // para ser publicado (se publicará al llamar a connect())
    serializeJson(conexion, sconexion);

    // Intentamos conectarnos. Además del clientId, user y pass, le pasamos: 
    // - el topic LWT: el msg LWT se enviará a los clientes que estén suscritos a este topic
    // - QoS: 2
    // - retain flag: true, para que este msg sustituya al msg retenido cuando se pierda la conex
    // - El msg LWT, que es el JSON {"online": false}
    if (client.connect(clientId.c_str(),user,pass,"infind/GRUPO8/conexion",2,true,sconexion)) 
    {
      // Construimos ahora el msg retenido, que es el mismo que el LWT pero con online=true
      conexion["online"] = true;
      // Lo serializamos en sconexion
      serializeJson(conexion, sconexion);   
      // Y lo publicamos en el topic /conexion con el retain flag puesto a true
      client.publish("infind/GRUPO8/conexion",sconexion,true);

      // Informamos por consola que nos hemos conectado
      Serial.println("connected");
      //Nos suscribimos al topic para obtener las configuraciones necesarias
      client.subscribe("infind/GRUPO8/config");
      
    } 
    else 
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}
void actualizar(){
  // ACTUALIZACIÓN OTA
  Serial.println( "--------------------------" );  
  Serial.println( "Comprobando actualización:" );
  Serial.print(HTTP_OTA_ADDRESS);Serial.print(":");Serial.print(HTTP_OTA_PORT);Serial.println(HTTP_OTA_PATH);
  Serial.println( "--------------------------" );  
  ESPhttpUpdate.setLedPin(16,LOW);
  ESPhttpUpdate.onStart(inicio_OTA);
  ESPhttpUpdate.onError(error_OTA);
  ESPhttpUpdate.onProgress(progreso_OTA);
  ESPhttpUpdate.onEnd(final_OTA);

  // La siguiente función comprueba si hay un programa nuevo para nuestro dispositivo, si lo
  // hay se lo descarga y lo sustituye por el actual. Una vez sustituido, se reinicia la placa
  // y comienza a ejecutarse el nuevo programa. Le tenemos que decir la dirección IP, puerto y
  // path donde está el servidor de actualizaciones: lo tenemos implementado en NodeRED
  switch(ESPhttpUpdate.update(HTTP_OTA_ADDRESS, HTTP_OTA_PORT, HTTP_OTA_PATH, HTTP_OTA_VERSION)) 
  {
    case HTTP_UPDATE_FAILED:
      Serial.printf(" HTTP update failed: Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println(F(" El dispositivo ya está actualizado"));
      break;
    case HTTP_UPDATE_OK:
      Serial.println(F(" OK"));
      break;
  }  
  
}

//-------------- setup --------------//

void setup() 
{
  pinMode(16, OUTPUT);
  pinMode(BUILTIN_LED, OUTPUT);
  Serial.begin(115200);
  setup_wifi();
  dht.setup(5, DHTesp::DHT11);
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  // Establecemos el tamaño del buffer para el manejo de msgs MQTT a 512 (msgs largos)
  client.setBufferSize(512);
  actualizar();
  Serial.begin(115200);
  Serial.println();
  LED();
  
  button.setClickHandler(handler);
  button.setDoubleClickHandler(handler);
  button.setTripleClickHandler(handler);
  button.setLongClickHandler(longpress);
}

//---------Funciones Utiles---------//

void LED(){
  // Actualizamos constantentemente el valor PWM que estamos mandando al LED, para que
  // si llega un nuevo valor de level, actualicemos el brillo del LED
  // El analogWrite tenemos que llamarlo con un valor PWM entre 0 y 1023: [0,100]->[0-1023]
  // Además, el LED funciona con lógica negativa: 1023->0 y 0->1023
  PWM = 1023-(level*1023/100);
  Serial.print("HOLA Q ASE HE ENTRAO NINIO");
  if(LedDigital)digitalWrite(16,HIGH);
  else digitalWrite(16,LOW);
  // Formateamos el msg a publicar en la estructura JSON ledstatus
  StaticJsonDocument<256> ledstatus;
  // Creamos la clave "led" y le asociamos el valor de led
  ledstatus["led"]=level;
  ledstatus["LedDigital"]=LedDigital;
  // Serializamos la estructura JSON ledstatus en el string sledstatus y lo publicamos  
  serializeJson(ledstatus, sledstatus);
  client.publish("infind/GRUPO8/led/status",sledstatus);
}

//----controlador led a traves del flash----//

void longpress(Button2& btn) {
    unsigned int time = btn.wasPressedFor();
    Serial.print("You clicked ");
    if (time >= 4000) {
      actualizar();
    }
    Serial.print(" (");        
    Serial.print(time);        
    Serial.println(" ms)");
}

void handler(Button2& btn) {
    switch (btn.getClickType()) {
        case SINGLE_CLICK:
              Serial.print("Se detecto una pulsacion y el led esta a ");
      
      Serial.print(1023-PWM);
      Serial.print(" de intensidad");
      if(led_control){
         PWMflash=PWM;
         PWMold=PWM;
         PWM=1023;
         led_control=false;
         Serial.print("\n");
         Serial.print("apagando");
         Serial.print("\n");
         LedDigital=false;
         LED();
      }
      else{
        PWMold=PWM;
        PWM=PWMflash;
        led_control=true;
        Serial.print("\n");
        Serial.print("encendiendo");
        Serial.print("\n");
        LedDigital=true;
        LED();
        }
            break;
        case DOUBLE_CLICK:
            Serial.print("double ");
            PWM=0;
            break;
        case TRIPLE_CLICK:
            Serial.print("triple ");
            break;
    }
    Serial.print("click");
    Serial.print(" (");
    Serial.print(btn.getNumberOfClicks());    
    Serial.println(")");
}

//funcion auxiliar para actualizar
/*void comprueboactualizacion(){
  if(actualizaMQTT || now-lastActualizacion>frecActualizacion){
    if(frecActualiza==0)return;
    lastActualizacion=now;
    actualizaMQTT=false;
    actualizar();
  }
}*/

//-------------- loop --------------//

void loop() 
{
  if (!client.connected()) {
    reconnect();
  }

  client.loop();
  button.loop();
  
  unsigned long now = millis();  
  if (now - lastMsg > Temp) 
  {
    lastMsg = now;

    // Leemos los datos del sensor
    delay(dht.getMinimumSamplingPeriod());
    float humidity = dht.getHumidity();
    float temperature = dht.getTemperature();

    // Leemos el voltaje Vcc observado en la entrada de alimentación y lo pasamos a V
    float vcc = ESP.getVcc()/1000.0;

    // Obtenemos el RSSI de la conexión WiFi
    float rssi = WiFi.RSSI();
    // Obtenemos la IP del router al que estamos conectados. Esa función me devuelve un
    // valor de tipo IPAddress -> lo pasamos a string
    String IP = WiFi.localIP().toString();

    // Formateamos los datos a publicar en la estructura JSON datos
    StaticJsonDocument<512> datos;

    // El campo Uptime es el número de ms desde que se inició la placa
    datos["Uptime"] = now;
    datos["Vcc"] = vcc;
    datos["LED"] = level;

    // Creamos el objeto anidado dht11, perteneciente a la estructura JSON datos
    JsonObject dht11 = datos.createNestedObject("DHT11");
    dht11["Temperatura"] = temperature;
    dht11["Humedad"] = humidity;

    // Creamos el objeto anidado wifi, perteneciente a la estructura JSON datos
    JsonObject wifi = datos.createNestedObject("WiFi");
    wifi["SSId"] = ssid;
    wifi["IP"] = IP;
    wifi["RSSI"] = rssi;

    // Serializamos la estructura JSON datos en el string sdatos y lo publicamos
    serializeJson(datos, sdatos);
    client.publish("infind/GRUPO8/datos",sdatos);
  }

  //Se llama a las funciones utiles para obtener el estado de los sensores/actuadores
  /*Serial.print("\n");
  Serial.print(PWM);
  Serial.print("     ");
  Serial.print(PWMold);
  Serial.print("\n");
  */
  if(PWMold<=PWM && now - lastPWM  > frecled){     
    lastPWM=now;
    PWMold++;
    analogWrite(2,PWMold);

  }
  if(PWMold>=PWM && now - lastPWM  > frecled){
    lastPWM=now;
    PWMold--;
    analogWrite(2,PWMold);
  }  
 
  //comprueboactualizacion();
    if(actualizaMQTT || now-lastActualizacion>frecActualizacion){
      if(frecActualizacion==0 && actualizaMQTT==false)return;
      lastActualizacion=now;
      actualizaMQTT=false;
      actualizar();
  }
}

//-------------- Funciones OTA --------------//

void final_OTA(){
  Serial.println("Fin OTA. Reiniciando...");}

void inicio_OTA(){
  Serial.println("Nuevo Firmware encontrado. Actualizando...");}

void error_OTA(int e){
  char cadena[64];
  snprintf(cadena,64,"ERROR: %d",e);
  Serial.println(cadena);}

void progreso_OTA(int x, int todo){
  char cadena[256];
  int progress=(int)((x*100)/todo);
  if(progress%10==0)
  {
    snprintf(cadena,256,"Progreso: %d%% - %dK de %dK",progress,x/1024,todo/1024);
    Serial.println(cadena);
  }
}
