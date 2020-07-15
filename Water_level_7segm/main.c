	/**********************************************************************************************
	*                                                                                             *
	*      Индикатор уровня воды с ультразвуковым датчиком HC-SR04 (7-сегментный индикатор)       *
	*                                                                                             *
	*                                  ATtiny2313A     4.0 MHz                                    *
	*                                                                                             *
	**********************************************************************************************/

#include <avr/io.h>
#include <avr/interrupt.h>

//#define PROTEUS																			// Отладка в Proteus (датчик будет делать замеры не по WDT, а по таймеру)
																						// отключение отладки - закомментировать
#define PORTSR04		PORTD
#define DDRSR04			DDRD
#define SR04_trig		0		// PD0
#define SR04_echo		2		// PD2
#define Razr_1			6		// PD6
#define Razr_2			5		// PD5
#define Razr_3			4		// PD4
#define Pump			1		// PD1
#define BTN				3		// PD3
#define Razr_1_ON		PORTD |= (1<<Razr_1)
#define Razr_2_ON		PORTD |= (1<<Razr_2)
#define Razr_3_ON		PORTD |= (1<<Razr_3)
#define Razr_1_OFF		PORTD &=~ (1<<Razr_1)
#define Razr_2_OFF		PORTD &=~ (1<<Razr_2)
#define Razr_3_OFF		PORTD &=~ (1<<Razr_3)
#define Pump_ON			PORTD &=~ (1<<Pump)
#define Pump_OFF		PORTD |= (1<<Pump)
#define Sym_Defis		10																// символ "дефис" из массива Number
#define Sym_F			11																// символ "F" из массива Number
#define Sym_U			12																// символ "U" из массива Number
#define Sym_L			13																// символ "L" из массива Number
#define Sym_n			14																// символ "n" из массива Number
#define Sym_NUL			15																// символ "пусто" из массива Number
#define Sym_3def		16																// символ "---" из массива Number
#define TimerClock_us	2																// длительность такта таймера, us - из настроек таймера в Presets()
#define Level_max		164																// максимальный уровень воды, л (для индикации FUL)
#define Level_pump_on	5																// уровень для включения насоса после выключения
#define Count_max_btn	120																// max значение счетчика (антиюребезг кнопки) (120 * 3.3 ms = 400 ms)
#define Average_factor	8																// коэффициент усреднения
#define _echo_ongoing	0																// флаг приема импульса Echo (бит 0)
#define _echo_ended		1																// флаг окончания приема импульса Echo (бит 1)
#define _low_level		2																// флаг аварийно низкого уровня воды (бит 2)
#define _pump_is_off	3																// флаг отключенного насоса
#define _btn_pressed	4																// флаг нажатой кнопки
#define _mode_on		5																// флаг режима ON
#define _mode_off		6																// флаг режима OFF
#define _array_full		7																// флаг заполненного массива усреднения

volatile unsigned long	EchoTimerCount = 0;												// число тактов счетчика (продолжительность Echo) (long - для расчета)
volatile unsigned char	Flag_byte = 0;													// флаговая переменная
volatile signed int		Dig_Ind = 0;													// объем в литрах
volatile signed int		Dig_Ind_Avr = 0;												// объем в литрах (усредненное значение)
volatile unsigned char	Sym_Razr_1 = Sym_3def;											// символ для индикации разряд 1
volatile unsigned char	Sym_Razr_2 = Sym_3def;											// символ для индикации разряд 2
volatile unsigned char	Sym_Razr_3 = Sym_3def;											// символ для индикации разряд 3
volatile unsigned char	Curr_Razr =	1;													// текущий разряд для отображения
volatile unsigned char	CountTime;														// счетчик (инкрементируется по таймеру 0)
volatile unsigned char	Arr_Average_Position = 0;										// указатель на элемент массива усреднения
unsigned char Symbol [17] = {63, 6, 91, 79, 102, 109, 125, 7, 127, 111, 64, 113, 62, 56, 84, 0, 73};		//DOT - 128
int Arr_Average [Average_factor];

void Presets(void);
void SendTrig(void);
void SetSymbols(void);
void ChangeMode(void);
int Averaging(int);


	/**********************************************************************************************
	*                                     ОСНОВНАЯ ФУНКЦИЯ                                        *
	**********************************************************************************************/

int main(void)
{
	Presets();
	
	while (1)
	{
		if (Flag_byte & (1<<_echo_ended))												// если обнаружено завершенное сканирование
		{
			if (EchoTimerCount <= 460)													// расчет по формулам в трех диапазонах (см. файл .xlsx)
				Dig_Ind = 182 - EchoTimerCount * 182 / 1620;
			else if (EchoTimerCount >= 1260)	
				Dig_Ind = 162 - EchoTimerCount * 162 / 1495;
			else								
				Dig_Ind = 190 - EchoTimerCount * 190 / 1455;			
			
			Dig_Ind_Avr = Averaging(Dig_Ind);											// Усреднение получаемых результатов измерения (ликвидация скачков)

			SetSymbols();																// разделение полученного значения по разрядам индикатора
			Flag_byte &=~ (1<<_echo_ended);												// сброс флага об имеющемся результате замера
		}
		
		if ((Flag_byte & (1<<_btn_pressed)) && (CountTime >= Count_max_btn))			// нажата кнопка и отработан антидребезг
		{
			ChangeMode();																// смена режима
			Flag_byte &=~ (1<<_btn_pressed);											// сброс флага нажатия кнопки
			GIMSK |= (1<<INT1);															// разрешение прерывания INT1 (ожидание след. нажатия)
		}
		
		asm("nop");
		
		#ifdef PROTEUS
			if ((!(Flag_byte & (1<<_mode_on))) && (!(Flag_byte & (1<<_mode_off))))
			if (CountTime > 80)															// цифра не меньше Count_max_btn
			{
				SendTrig();
				CountTime = 0;
			}
		#endif
	}
}


	/**********************************************************************************************
	*                                    ОСТАЛЬНЫЕ ФУНКЦИИ                                        *
	**********************************************************************************************/

		/*-------------------------------- Предустановки ------------------------------------*/

void Presets(void)
{
	DDRB = 0xff;																	// настройка портов
	DDRD |= (1<<Razr_1) | (1<<Razr_2) | (1<<Razr_3) | (1<<Pump);
	DDRSR04 |= (1<<SR04_trig);
	PORTB = 0;
	Razr_1_OFF; Razr_2_OFF; Razr_3_OFF;
	PORTSR04 &=~ (1<<SR04_trig);
	Pump_ON;
	PORTD |= (1<<BTN);
																					// таймер 0
	TCCR0A |= (1<<WGM01);																// режим работы таймера 0 (т. 11.8 стр. 84)		CTC
	OCR0A = 51;																			// конечное значение счетчика (11.9.4 стр. 75)	15625/(51+1)=300Hz  (3.3 ms)
	TCCR0B |= (1<<CS02);																// предделитель таймера 0 (т. 11.9 стр. 86)		4000000/256=15625 Hz
	TIMSK |= (1<<OCIE0A);																// предывание при совпадении А
																					// таймер 1
	TCCR1B|= (1<<CS11);																	// предделитель таймера 1 (т. 12.6 стр. 114)	4000000/8=500000 Hz (2 us)
																					// прерывание по нажатю кнопки
	MCUCR |= (1<<ISC11);																// по ниспадающему фронту
//////////////////////////////////////////////////////////////////////////
//	GIMSK |= (1<<INT1);																	// разрешение прерывания INT1
	#ifndef PROTEUS																	// таймер WDT
		asm ("cli");																	// глобальный запрет прерываний на время конфигурирования WDT
		asm ("wdr");																	// сброс счетчика WDT
		WDTCR |= (1<<WDCE) | (1<<WDE);
		WDTCR |= (1<<WDE) | (1<<WDP2) | (1<<WDP1) | (1<<WDP0);							// предделитель 256k - 2 сек. (повторяется в ChangeMode()) (стр. 47)
		WDTCR |= (1<<WDIE);																// включение WDT в режиме прерываний
	#endif
	asm("sei");
}


        /*------------------------ Отправка сигнала на измерение ----------------------------*/
        
void SendTrig(void)
{
	PORTSR04 |= (1<<SR04_trig);															// Начало импульса
	for (unsigned char temp=0; temp<13; temp++)	asm("nop");								// Задержка (~54 такта ~13 us)
	PORTSR04 &=~ (1<<SR04_trig);														// Конец импульса
	        
	MCUCR |= (1<<ISC01) | (1<<ISC00);													// Прерывание по возрастающему фронту INT0 (ожидание начала импульса Echo)
	GIMSK |= (1<<INT0);																	// Разрешение прерывания INT0
}

        /*----------------------------- Усреднение значений ---------------------------------*/    // скользящее среднее
        
int Averaging(int Dig)
{
	Arr_Average[Arr_Average_Position] = Dig;											// Запись в текущий элемент массива нового значения
	Arr_Average_Position = (Arr_Average_Position + 1) % Average_factor;					// Перевод указателя на следующий элемент массива
	
	if ((Flag_byte & (1<<_array_full)) == 0)											// если массив пустой
	{
		for (unsigned char a=1; a < Average_factor; a++)								// заполнение массива одинаковыми значениями
		{
			Arr_Average [a] = Dig;
		}
		
		Flag_byte |= (1<<_array_full);													// установка флага заполненного массива
	}
	
	Dig = 0;
	for (unsigned char i=0; i < Average_factor; i++)
	{
		Dig = Dig + Arr_Average[i];														// Суммирование всех элементов массива
	}
	return Dig / Average_factor;														// Результат --> сумма всех элементов / количество элеметов
	//	return Dig >> 3;																// сдвиг вмето деления на степень двойки Average_Factor
}

        /*----------------------- Разделение числа на три разряда ---------------------------*/
        
void SetSymbols(void)
{
	asm("cli");
	
	Sym_Razr_1 = 0;	Sym_Razr_2 = 0; Sym_Razr_3 = 0;										// сброс предыдущих значений
	
	if (Dig_Ind_Avr <= 0)																// если уровень воды ниже 0
	{
		Sym_Razr_1 = Sym_Defis;
		Sym_Razr_2 = Sym_Defis;
		Sym_Razr_3 = Sym_Defis;
		if (!(Flag_byte & (1<<_pump_is_off)))											// если насос включен
		{
			Pump_OFF;																	// выключение насоса
			Flag_byte |= (1<<_pump_is_off);												// установка флага выключенного насоса
		}
	}
	else if (Dig_Ind_Avr > Level_max)													// если уровень воды > max
	{
		Sym_Razr_1 = Sym_F;
		Sym_Razr_2 = Sym_U;
		Sym_Razr_3 = Sym_L;
	}
	else																				// если уровень в пределах нормы
	{
		if (Flag_byte & (1<<_pump_is_off))												// если насос выключен
		{
			if (Dig_Ind_Avr >= Level_pump_on)											// если уровень достиг порога включения
			{
				Pump_ON;																// включение насоса
				Flag_byte &=~ (1<<_pump_is_off);										// сброс флага
			}
		}
		
		while (Dig_Ind_Avr >= 100)														// разделение на сотни, десятки, единицы
		{
			Sym_Razr_1 ++;
			Dig_Ind_Avr -= 100;
		}
		while (Dig_Ind_Avr >= 10)
		{
			Sym_Razr_2 ++;
			Dig_Ind_Avr -= 10;
		}
		Sym_Razr_3 = Dig_Ind_Avr;
	}
	
	asm("sei");
}

        /*----------------------------- Смена режима работы ---------------------------------*/
        
void ChangeMode(void)
{
	if ((!(Flag_byte & (1<<_mode_on))) && (!(Flag_byte & (1<<_mode_off))))		// если активен режим AUTO
	{
		asm ("cli");
		asm ("wdr");
		WDTCR |= (1<<WDCE) | (1<<WDE);
		WDTCR = 0;																		// выкл. измерения расстояния (WDT)
		Flag_byte |= (1<<_mode_on);														// вкл. режима ON
		Pump_ON;
		Flag_byte &=~ (1<<_pump_is_off);												// вкл. насоса
		Sym_Razr_1 = 0;
		Sym_Razr_2 = Sym_n;
		Sym_Razr_3 = Sym_NUL;															// индикация "On "
		asm("sei");
	} 
	else if (Flag_byte & (1<<_mode_on))											// если активен режим ON
	{
		asm ("cli");
		Flag_byte &=~ (1<<_mode_on);													// выкл. режима ON
		Flag_byte |= (1<<_mode_off);													// вкл. рефима OFF
		Pump_OFF;
		Flag_byte |= (1<<_pump_is_off);													// выкл. насоса
		Sym_Razr_1 = 0;
		Sym_Razr_2 = Sym_F;
		Sym_Razr_3 = Sym_F;																// индикация "OFF"
		asm("sei");
	}
	else if (Flag_byte & (1<<_mode_off))										// если активен режим OFF
	{
		asm("cli");
		Flag_byte &=~ (1<<_mode_off);													// выкл. режима OFF
		#ifndef PROTEUS
			asm ("wdr");
			WDTCR |= (1<<WDCE) | (1<<WDE);
			WDTCR |= (1<<WDE) | (1<<WDP2)| (1<<WDP1)| (1<<WDP0);
			WDTCR |= (1<<WDIE);															// вкл. измерения расстояния (WDT)
		#endif
		asm("sei");
		SendTrig();																		// измерение не дожидаясь срабатывания WDT
	}
}


	/**********************************************************************************************
	*                                         ПРЕРЫВАНИЯ                                          *
	**********************************************************************************************/

		/*------------------------------ По совпадению счетчика -----------------------------*/

ISR (TIMER0_COMPA_vect)																	// 3.3 ms
{
	switch (Curr_Razr)
	{
		case 1:
			Razr_2_OFF; Razr_3_OFF; 
			PORTB = Symbol [Sym_Razr_1];
			Curr_Razr ++;
			if ((!(Flag_byte & 1<<_mode_on)) && (!(Flag_byte & 1<<_mode_off)) && (Sym_Razr_1 == 0)) break;	// отключение незначащего нуля в режиме AUTO
			Razr_1_ON;
			break;
		case 2:
			Razr_1_OFF; Razr_3_OFF;
			PORTB = Symbol [Sym_Razr_2];
			Curr_Razr ++;
			if ((Sym_Razr_1 == 0) && (Sym_Razr_2 == 0)) break;							// отключение незначащего нуля
			Razr_2_ON;
			break;
		case 3:
			Razr_1_OFF; Razr_2_OFF;
			PORTB = Symbol [Sym_Razr_3];
			Curr_Razr = 1;
			Razr_3_ON;
			break;
	}	
	
	CountTime++;
}

		/*------------------------------- По таймеру Watchdog -------------------------------*/
#ifndef PROTEUS
ISR (WDT_OVERFLOW_vect)																	// 2.0 s
{
	SendTrig();																			// отправка импульса на Trig датчика
	
	WDTCR |= (1<<WDIE);
}
#endif

		/*--------------------- При поступлении сигнала с датчика SR04 ----------------------*/
        
ISR (INT0_vect)
{
	if ((Flag_byte & (1<<_echo_ongoing)) == 0)											// Если Echo только пошел (_echo_ongoing == 0)
	{
		TCNT1 = 0;																		// Сброс счетчика таймера
		Flag_byte |= (1<<_echo_ongoing);												// Запись во флаг о начале према Echo
		MCUCR &=~ (1<<ISC00);															// Прерывание по убывающему фронту INT0 (ожидание конца импульса Echo)
	}
	else
	{
		EchoTimerCount = TCNT1;															// Сохранение количества тактов счетчика
		Flag_byte &=~ (1<<_echo_ongoing);												// Очистка флага приема импульса
		Flag_byte |= (1<<_echo_ended);													// Запись флага об окончании измерения
		GIMSK &=~ (1<<INT0);															// Запрет прерываний по INT0
	}
}

		/*------------------------------- При нажатии кнопки --------------------------------*/
/*		
ISR (INT1_vect)
{
	GIMSK &=~ (1<<INT1);																// запрет прерываний INT1 (не срабатывать, пока антидребезг не разрешит)
	CountTime = 0;																		// сброс счетчика
	Flag_byte |= (1<<_btn_pressed);														// установка флага нажатой кнопки
}
*/

// 143 и 353 строки закомментировал для отключения кнопки (смены режимов) во избежание 
// ложных срабатываний от работы насоса