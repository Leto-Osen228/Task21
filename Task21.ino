/*
   Автор: Петров Александр Алексеевич
   Дата: 30 Октября 2023

   Для измерения был выбран датчик CO2 AGS10, но он оказадся не исправным.
   Код написан для абстрактного сенсора, в роле которого выступил потенциометр.

   В качестве экрана выступил графический дисплей разрешения 128*64 с контроллером ssd1606.
   Для индикации использовался адресный RGB светодиод, установленный на плату.

   Для упрощения кода использовались внешнии библиотеки.
   
   Тестировалось на отладочной плате ESP32-C3 AI.
*/

#define ARGB_PIN 8          // Пин адресного светодиода
#define SENSOR_PIN 4        // Пин аналогового датчика
#define BUZZER_PIN 10       // Пин пищалки 

#define SDA_PIN 6           // Пин данных I2C
#define SCL_PIN 7           // Пин тактирования I2C

#define OK_BUTTON_PIN 1     // Пин кнопки "ОК" (следующий пункт)
#define LEFT_BUTTON_PIN 3   // Пин кнопки "LEFT" (меньше / выкл)
#define RIGHT_BUTTON_PIN 2  // Пин кнопки "RIGHT" (больше / вкл)

// Подключение библиотек, инициализация объекта дисплея
#include <Wire.h>
#include "GyverOLED.h"
GyverOLED<SSD1306_128x64> oled;

// Подключение библиотек, инициализация объектов кнопок
#include "GyverButton.h"
GButton ok_btn(OK_BUTTON_PIN);
GButton left_btn(LEFT_BUTTON_PIN);
GButton right_btn(RIGHT_BUTTON_PIN);

// Подключение библиотек, инициализация объекта светодиода
#include "FastLED.h"
CRGB leds[1];

// Перечисление состояний устройства
typedef enum {
  Ok,
  Control,
  Critical
} State;


typedef struct Setting {
  // влючена ли писчалка
  bool buzzer_en = 0;

  // порог некритического превышения уровня газа
  uint16_t medium_threshold = 1000;

  // порог критического превышения уровня газа
  uint16_t high_threshold = 2000;

  // градиентная светодиодная индикация
  bool gradient_led_display = 0;
} Setting;

uint16_t value;   // Текущий оровень газа в попугаях
State state;      // Текущее состояние
Setting setting;  // Структура с настройками

/*
    Функция содержащая основную логику работы устройства
 */
void task(void *pvParameters) {
  // Установка пинов в качестве выходов
  pinMode(BUZZER_PIN, OUTPUT);

  // Установка разрешения АЦП на 12 бит
  analogReadResolution(12);

  // Инициализация адреного светодиода, установка яркости ~40%
  FastLED.addLeds<WS2812B, ARGB_PIN, GRB>(leds, 1);
  FastLED.setBrightness(100);
  FastLED.clear();

  // Инициализация дисплея
  oled.init(SDA_PIN, SCL_PIN);
  oled.clear();
  for (;;) {
    updateState();
    display();
    buzzerHandler();
  }
}

/*
   Функция обновления состояния.
   Принимает: ничего
   Возвращает: ничего
   
   Считывает и фильтрует значения, определяет состояние, устанавливает цвет индикатора.
*/
void updateState() {
  static uint32_t timer;
  if (millis() - timer > 5) {
    uint16_t now_val = analogRead(SENSOR_PIN);

    // Фильтр низких частот
    value += (now_val - value) / 10;

    if (value < setting.medium_threshold) {
      state = Ok;
      leds[0] = CRGB::Green;
    } else if (value < setting.high_threshold) {
      state = Control;
      // Если включен градиентовый режим
      if ( setting.gradient_led_display) {
        // Индицировать градиент зелёный - красный
        uint8_t color = map(value, setting.medium_threshold, setting.high_threshold, 0, 255);
        leds[0] = CRGB(color, 255 - color, 0);
      } else {
        // Иначе жёлтый
        leds[0] = CRGB::Yellow;
      }
    } else {
      state = Critical;
      leds[0] = CRGB::Red;
    }
    FastLED.show();
  }
}

/*
   Функция работы с пользователем, вывод на дисплей, обработка нажатий.
   Принимает: ничего
   Возвращает: ничего
   
*/
void display(void) {
  bool update_flag = 1;       // флаг обновления дисплея
  int8_t step = 0;            // Переменная изменения параметров
  static int8_t cursor = 0;   // Курсор меню
  static State old_state;     // Старое состояние устройства

  // Если состояние изменилось
  if (old_state != state) {
    old_state = state;
    update_flag = 1;
  }

  // Обновление состояния кнопок
  ok_btn.tick();
  left_btn.tick();
  right_btn.tick();

  if (ok_btn.isClick())
    cursor = (cursor + 1) % 4;
  if (right_btn.isClick())
    step += 10;
  if (right_btn.isStep())
    step += 100;
  if (left_btn.isClick())
    step -= 10;
  if (left_btn.isStep())
    step -= 100;

  // Если нужно изменить параметры
  if (step != 0) {
    // Меняем порог изменения состояния или переключаем функции
    switch (cursor) {
      case 0:
        if (step > 0) setting.buzzer_en = true;
        else setting.buzzer_en = false;
        break;
      case 1:
        setting.medium_threshold += step;
        break;
      case 2:
        setting.high_threshold += step;
        break;
      case 3:
        if (step > 0) setting.gradient_led_display = true;
        else setting.gradient_led_display = false;
        break;
    }
    update_flag = true;
  }
  
  if (update_flag) {
    // Обновление информации на дисплее
    oled.clear();
    switch (state) {
      case Ok:
        oled.setCursor(0, 0);
        oled.print("Всё хорошо");
        break;
      case Control:
        oled.setCursor(0, 0);
        oled.print("Осторожно");
        break;
      case Critical:
        oled.setCursor(0, 0);
        oled.print("Опасно");
        break;
    }

    oled.setCursor(0, cursor + 1);
    oled.print(">");
    oled.setCursor(8, 1);
    oled.printf("Пищалка : %s", setting.buzzer_en ? "вкл" : "выкл");
    oled.setCursor(8, 2);
    oled.printf("Порог 1 : %04d", setting.medium_threshold);
    oled.setCursor(8, 3);
    oled.printf("Порог 2 : %04d", setting.high_threshold);
    oled.setCursor(8, 4);
    oled.printf("Градиент: %s", setting.gradient_led_display ? "вкл" : "выкл");
    oled.update();
    update_flag = 0;
  }
}

/*
   Функция писка.
   Принимает: ничего
   Возвращает: ничего
   Писщит соответственно режиму.
*/
void buzzerHandler() {
  if (setting.buzzer_en == false){
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }
  uint32_t time = millis();
  switch (state) {
    case Ok:
      // выкл
      digitalWrite(BUZZER_PIN, LOW);
      break;
    case Control:
      // 2Гц 10%
      time %= 500;
      digitalWrite(BUZZER_PIN, time < 50);
      break;
    case Critical:
      // 10Гц 50%
      time %= 100;
      digitalWrite(BUZZER_PIN, time < 50);
      break;
  }
}

void setup() {
  // Потребовалось создавать свою задач
  xTaskCreateUniversal(task, "task", 8192, NULL, 5, NULL, 1);
}

void loop() {
  delay(500);
}
