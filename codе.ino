#include <stDHT.h> //библиотека для работы с датчиком серии DHT
#include <Wire.h> //библиотека для работы с интерфейсов I2C
#include <LiquidCrystal_I2C.h> //библиотека для дисплея подключенного через I2C
#include <TroykaMQ.h> //библиотека для работы с датчиками серии MQ
#include <EEPROM.h> //библиотека для работы с памятью

//--------------------------------------------------->CONSTANTS<---------------------------------------------------//
#define MENU_BUTTON_PIN 2 //номер вывода на плате для подключения кнопки перехода в меню
#define BLUE_LED_PIN 4 //номер вывода на плате для подключения синего светодиода
#define YELLOW_LED_PIN 5 //номер вывода на плате для подключения желтого светодиода
#define RED_LED_PIN 6 //номер вывода на плате для подключения красного светодиода
#define BUZZER_PIN 7 //номер вывода на плате для подключения зуммера
#define DHT22_SENSOR_PIN 8 //номер вывода на плате для подключения датчика температуры и влажности
#define RELAY_PIN 9 //номер вывода на плате для подключения реле
#define ANALOG_KEYBOARD_PIN A0 //номер вывода на плате для подключения резистивной клавиатуры
#define MQ135_SENSOR_PIN A1 //номер вывода на плате для подключения датчика углекислого газа
#define MQ4_SENSOR_PIN A2 //номер вывода на плате для подключения датчика метана
#define WARMING_UP_TIME 60000 //время прогрева датчиков газа, равное 60с
#define WARMING_UP_INTERVAL 3000 //временной интервал для вывода прогресса прогревания, равный 3с
#define SENSOR_POLLING_INTERVAL 2000 //временной интервал для опроса датчиков, равный 2с
#define ERROR 20 //допустимое отклонение от значения АЦП при анализе нажатой кнопки
#define ANALOG_BUTTONS_NUM 4 //количество аналоговых кнопок
//--------------------------------------------------->CONSTANTS<---------------------------------------------------//


//--------------------------------------------------->GLOBAL VARIABLES<---------------------------------------------------//
LiquidCrystal_I2C display(0x27, 20,4); //объявляем объект библиотеки, указывая параметры дисплея
DHT sensorDHT22(DHT22); //объявляем  объект библиотеки, указывая модель датчика температуры
MQ4 sensorMQ4(MQ4_SENSOR_PIN); //объявляем  объект библиотеки, указывая пин подключения к arduino
MQ135 sensorMQ135(MQ135_SENSOR_PIN); //объявляем  объект библиотеки, указывая пин подключения к arduino
volatile int menuFlag = 0; //переменная-флаг входа в меню
unsigned long time = 0; //переменная, запоминающая время (в мс) с момента запуска системы
int temperature = 0; //переменная, храненящая температуру воздуха
int humidity = 0; //переменная, храненящая влажность воздуха
unsigned long concentrationCH4 = 0; //переменначя, храненящая концентрации CH4 в ppm
unsigned long concentrationCO2 = 0; //переменначя, храненящая концентрации CO2 в ppm
int maxTemperature = 25; //максимальное допустимое значение температуры в градусах Цельсия
int maxHumidity = 60; //максимальное допустимое значение влажности воздуха в процентах
unsigned long maxCH4Concentration = 4000; //максимальное допустимое значение CH4 в ppm
unsigned long maxCO2Concentration = 2000; //максимальное допустимое значение CO2 в ppm
byte signalingFlags = 0; //режим запускаемой сигнализации
int values[4] = {254, 498, 774, 1023}; //значения АЦП для при нажатии на 1 из 4 кнопок
//--------------------------------------------------->GLOBAL VARIABLES<---------------------------------------------------//


//--------------------------------------------------->FUNCTIONS<---------------------------------------------------//
//Обработчик 0-ого прерывания. Изменяет значение флага меню
int changeMenuFlag(){
  menuFlag = (menuFlag == 1) ? 0 : 1;
}

//опрделение номера кнопки по значению АЦП
int buttonNumber(int value){
  int i = 0; //переменная цикла, прохода по массиву допустимых АЦП
  //цикл прохода по массиву возможных значений АЦП
  for(; i < ANALOG_BUTTONS_NUM; i++){
    //проверка значения из массива АЦП с учетом погрешности
    if((value <= (values[i] + ERROR)) && (value >= (values[i] - ERROR))){ 
      return i + 1; //вернуть номер кнопки 
    }
  }
  return -1; //нет нажатых кнопок, вернуть -1
}

//функция проверки нажатия кнопок на клавиатуре
int analogKeyBoard(){
  static int count; //счетчик для определения дастоверности нажатия кнопки
  static int oldKeyValue; //старое значение аналоговой клавиатуры
  static int innerKeyValue; //для хранения нажатой клавиши на протяжении 10 проходов

  int actualKeyValue = buttonNumber(analogRead(ANALOG_KEYBOARD_PIN)); //получить текущую нажатую кнопку
  //проверяем действильно ли нажато кнопка путем 10 проходов по алгоритму
  if(innerKeyValue != actualKeyValue){
    count = 0; //обнуление счетчика(все снчала)
    innerKeyValue = actualKeyValue; //обновить текущее значения полученное с обработчика клавитуры
  }
  else{
    count++; //увеличить счетчик проходов
  }
  //кнопка нажата и старое значение не совпадает с актуальным
  if((count >= 10) && (oldKeyValue != actualKeyValue)){
    oldKeyValue = actualKeyValue; //запоминаем новое значение
  }
  return oldKeyValue;
}

//фцнкция прогрева и калибровки датчиков
void  gasSensorsCalibration(){
  unsigned long progressTime = 0; //для получения промежутков по 3с в цикле
  int position = 0; //указатель позиции на дисплее (степень прогрева)

  digitalWrite(BLUE_LED_PIN, HIGH); //подать напряжение на синий светодиод (начало прогрева датчиков)
  display.clear(); //очистить дисплей от символов
  display.setCursor(1,0); //установить курсор на позицию (0, 0)
  display.print("WARMING UP SENSORS"); //вывести надпись "warming up sensors"
  display.setCursor(6, 2); //установить курсор на позицию (6, 2)
  display.print("PROGRESS"); //вывести надпись "progress"
  time = millis(); //получить текущеее время работы
  //цикл задержки на 60с, время прогрева датчиков газа
  while((millis() - time) < WARMING_UP_TIME){
    //каждые три секунды обновлять поле progress
    if((millis() - progressTime) > WARMING_UP_INTERVAL){
      progressTime = millis(); //получить текущеее время работы
      display.setCursor(position++, 3); //установить курсор на позицию (position, 3)
      display.print("#"); //вывести символ '#'
    }
  }
  sensorMQ4.calibrate(); //выполнить калибровку датчика MQ4 на чистом воздухе
  sensorMQ135.calibrate(); //выполнить калибровку датчика MQ135 на чистом воздухе
  digitalWrite(BLUE_LED_PIN, LOW); //снять напряжения с синего светодиода
  display.clear(); //очистить дисплей от символов
}

//функция чтения показаний с датчиков
void readDataFromSensors(){
  temperature = sensorDHT22.readTemperature(DHT22_SENSOR_PIN); //снять значение температуры с датчика
  humidity = sensorDHT22.readHumidity(DHT22_SENSOR_PIN); //снять значение влажности с датчика
  concentrationCH4 = sensorMQ4.readMethane(); //снять значение концентрации метана
  concentrationCO2 = sensorMQ135.readCO2(); //снять значение концентрации углекислого газа
}

//вывод информации на дисплей 
void printInfo(unsigned long valueCH4, unsigned long valueCO2, int valueTemp, int valueHum, int linePointer){
  int startPosition = (linePointer == -1) ? 0 : 1; //если не в меню, то вывод с нулевой позиции, иначе с первой 

  display.clear(); //очистить дисплей
  display.setCursor(startPosition, 0); //установить курсор на позицию (startPosition, 0)
  display.print("CH4: "); //вывести название параметра 
  display.print(valueCH4); //вывести значениие
  display.print("PPM"); //вывести велечину измерения
  display.setCursor(startPosition, 1); //установить курсор на позицию (startPosition, 1)
  display.print("CO2: "); //вывести название параметра 
  display.print(valueCO2); //вывести значениие
  display.print("PPM"); //вывести велечину измерения
  display.setCursor(startPosition, 2); //установить курсор на позицию (startPosition, 2)
  display.print("TEMPERATURE: "); //вывести название параметра 
  display.print(valueTemp); //вывести значениие
  display.print("C"); //вывести велечину измерения
  display.setCursor(startPosition, 3); //установить курсор на позицию (startPosition, 3)
  display.print("HUMIDITY: "); //вывести название параметра 
  display.print(valueHum); //вывести значениие
  display.print("%"); //вывести велечину измерения
  if(linePointer != -1){ //если в режиме меню, то выводить указаетль '>' и '<'
    display.setCursor(0, linePointer); //установить курсор на позицию (0, linePointer)
    display.print(">"); //вывести указатель меню '>'
    display.setCursor(19, linePointer); //установить курсор на позицию (19, linePointer)
    display.print("<"); //вывести указатель меню '<'
  }
}

//функция анализа считанных с датчиков данных
void dataAnalysis(){
  signalingFlags = 0; //сбрасывание всех флагов

  //проверка на превышение CH4 и CO2
  if((concentrationCH4 > maxCH4Concentration) || (concentrationCO2 > maxCO2Concentration)){
    signalingFlags = signalingFlags | 1; //установить флаг 1
  }
  //проверка на превышения температуры и влажности
  if((temperature > maxTemperature) || (humidity > maxHumidity)){
    signalingFlags = signalingFlags | (1 << 1); //установить флаг 2
  }

  digitalWrite(RED_LED_PIN, LOW); //снять питание с красного светодиода
  digitalWrite(YELLOW_LED_PIN, LOW); //снять питание с желтого светодиода
  digitalWrite(RELAY_PIN, LOW); //закрыть реле
  noTone(BUZZER_PIN); //отключить пъезодинамик

  //определение типа сигнализирования по установленным флагам
  switch(signalingFlags){
    case 1: //превышают только газовые параметры
      digitalWrite(RED_LED_PIN, HIGH); //подать напряжение на красный светодиод
      digitalWrite(RELAY_PIN, HIGH); //открыть реле
      tone(BUZZER_PIN, 1500); //включить пъезодинамик 
      break;

    case 2: //превышает только температура или влажность
      digitalWrite(YELLOW_LED_PIN, HIGH); //подать напряжение на желтый светодиод
      tone(BUZZER_PIN, 1700); //включить пъезодинамик 
      break;
    
    case 3: //превышает температура или влажность, а также газовые параметры
      digitalWrite(RED_LED_PIN, HIGH); //подать напряжение на красный светодиод
      digitalWrite(YELLOW_LED_PIN, HIGH); //подать напряжение на желтый светодиод
      digitalWrite(RELAY_PIN, HIGH); //открыть реле
      tone(BUZZER_PIN, 1900); //включить пъезодинамик
      break;
  }
} 

//основное меню 
void menuFunc(){
  int menuPointer = 0; //указатель на пункт меню
  int currentKeyValue = 0; //значение нажатой кнопки
  int flag = 0; //флаг для определения момента отпускания кнопки
  
  //вывести меню с указателеме '> ... <' на дисплей
  printInfo(maxCH4Concentration, maxCO2Concentration, maxTemperature, maxHumidity, menuPointer);
  while(menuFlag == 1){ //если была нажата кнопка входа в меню
    currentKeyValue = analogKeyBoard(); //чтение значения с клавиатуры
    if((currentKeyValue != -1) && (flag == 0)){ //обработчик нажатия кнопки
      flag = 1; //кнопка нажата
      if(currentKeyValue == 1){ //если нажата 1 кнопка
        menuPointer++; //переход к следующему пункту меню
        //когда дошло до конца меню сброс в начало
        menuPointer = (menuPointer == 4) ? 0 : menuPointer;
      }
      else if(currentKeyValue == 2){ //если нажата 2 кнопка
        menuPointer--; //переход к предыдущему пункту меню
        //когда дошло до начала меню сброс в конец
        menuPointer = (menuPointer == -1) ? 3 : menuPointer;
      }
      else if(currentKeyValue == 3){ //если нажата 3 кнопка
        switch(menuPointer){ //в зависимости от пункта меню уменьшаем максимальное значение
          case 0: //menuPointer указывает на максимальную концентрацию CH4
            maxCH4Concentration -= 25;
            break;
          case 1: //menuPointer указывает на максимальную концентрацию CO2
            maxCO2Concentration -= 25;
            break;
          case 2: //menuPointer указывает на максимальную температуру
            maxTemperature -= 1;
            break;
          
          case 3: //menuPointer указывает на максимальную влажность
            maxHumidity -= 1;
            break;
        }
      }
      else if(currentKeyValue == 4){ //если нажата 4 кнопка
         switch(menuPointer){ //в зависимости от пункта меню увеличиваем максимальное значение
          case 0:  //menuPointer указывает на максимальную концентрацию CH4
            maxCH4Concentration += 25;
            break;

          case 1: //menuPointer указывает на максимальную концентрацию CO2
            maxCO2Concentration += 25;
            break;

          case 2: //menuPointer указывает на максимальную температуру
            maxTemperature += 1;
            break;
          
          case 3: //menuPointer указывает на максимальную влажность
            maxHumidity += 1;
            break;
        }
      }
    }

    if((currentKeyValue == -1) && (flag == 1)){ //обработчик отпускания кнопки
      flag = 0; //кнопки не нажаты
      //обновить меню на дисплее
      printInfo(maxCH4Concentration, maxCO2Concentration, maxTemperature, maxHumidity, menuPointer);
    }
  }
}

//чтения данных из eeprom (инициализация максимальных значений)
void initVariablesFromEEPROM(){
  EEPROM.get(0, maxCH4Concentration); //считать данные в maxCH4Concentration из памяти
  EEPROM.get(4, maxCO2Concentration); //считать данные в maxCO2Concentration из памяти
  EEPROM.get(8, maxTemperature); //считать данные в maxTemperature из памяти
  EEPROM.get(10, maxHumidity); //считать данные в maxHumidity из памяти
}

//запись данных в eeprom
void saveVariablesIntoEEPROM(){
  EEPROM.put(0, maxCH4Concentration); //записать данные из maxCH4Concentration в память
  EEPROM.put(4, maxCO2Concentration); //записать данные из maxCO2Concentration в память
  EEPROM.put(8, maxTemperature); //записать данные из maxTemperature в память
  EEPROM.put(10, maxHumidity); //записать данные из maxHumidity в память
}
//--------------------------------------------------->FUNCTIONS<---------------------------------------------------//

void setup() { //задание начальных параметров
  pinMode(MENU_BUTTON_PIN, INPUT); //установить режим вывода на прием сигнала
  pinMode(BLUE_LED_PIN, OUTPUT); //установить режим вывода на выдачу сигнала
  pinMode(YELLOW_LED_PIN, OUTPUT); //установить режим вывода на выдачу сигнала
  pinMode(RED_LED_PIN, OUTPUT); //установить режим вывода на выдачу сигнала
  pinMode(BUZZER_PIN, OUTPUT); //установить режим вывода на выдачу сигнала
  pinMode(DHT22_SENSOR_PIN, INPUT); //установить режим вывода на прием сигнала
  pinMode(RELAY_PIN, OUTPUT); //установить режим вывода на выдачу сигнала
  attachInterrupt(0, changeMenuFlag, RISING); //связываем прерывание с обработчиком
  display.init(); //инициализация дисплея
  display.backlight(); //включить подсветку дисплея
  initVariablesFromEEPROM(); //инициализировать максимльные значения
  gasSensorsCalibration(); //прогрев и калибровка датчиков
}

void loop() { //основной цикл программы
  //если прошло 2 секунды опрос датчиков
  if((millis() - time) > SENSOR_POLLING_INTERVAL){
    time = millis(); //обновить время работы контроллера
    readDataFromSensors(); //получить данные с датчиков
    //вывести информацию на дисплей с обновленными параметрами 
    printInfo(concentrationCH4, concentrationCO2, temperature, humidity, -1);
    dataAnalysis(); //анализ полученных данных
  }
  if(menuFlag == 1){ //зайти в меню 
    menuFunc();
    saveVariablesIntoEEPROM(); //запимсать значения в память
  }
}
