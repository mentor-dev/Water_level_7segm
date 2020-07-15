	/**********************************************************************************************
	*                                                                                             *
	*      ��������� ������ ���� � �������������� �������� HC-SR04 (7-���������� ���������)       *
	*                                                                                             *
	*                                  ATtiny2313A     4.0 MHz                                    *
	*                                                                                             *
	**********************************************************************************************/

#include <avr/io.h>
#include <avr/interrupt.h>

//#define PROTEUS																			// ������� � Proteus (������ ����� ������ ������ �� �� WDT, � �� �������)
																						// ���������� ������� - ����������������
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
#define Sym_Defis		10																// ������ "�����" �� ������� Number
#define Sym_F			11																// ������ "F" �� ������� Number
#define Sym_U			12																// ������ "U" �� ������� Number
#define Sym_L			13																// ������ "L" �� ������� Number
#define Sym_n			14																// ������ "n" �� ������� Number
#define Sym_NUL			15																// ������ "�����" �� ������� Number
#define Sym_3def		16																// ������ "---" �� ������� Number
#define TimerClock_us	2																// ������������ ����� �������, us - �� �������� ������� � Presets()
#define Level_max		164																// ������������ ������� ����, � (��� ��������� FUL)
#define Level_pump_on	5																// ������� ��� ��������� ������ ����� ����������
#define Count_max_btn	120																// max �������� �������� (����������� ������) (120 * 3.3 ms = 400 ms)
#define Average_factor	8																// ����������� ����������
#define _echo_ongoing	0																// ���� ������ �������� Echo (��� 0)
#define _echo_ended		1																// ���� ��������� ������ �������� Echo (��� 1)
#define _low_level		2																// ���� �������� ������� ������ ���� (��� 2)
#define _pump_is_off	3																// ���� ������������ ������
#define _btn_pressed	4																// ���� ������� ������
#define _mode_on		5																// ���� ������ ON
#define _mode_off		6																// ���� ������ OFF
#define _array_full		7																// ���� ������������ ������� ����������

volatile unsigned long	EchoTimerCount = 0;												// ����� ������ �������� (����������������� Echo) (long - ��� �������)
volatile unsigned char	Flag_byte = 0;													// �������� ����������
volatile signed int		Dig_Ind = 0;													// ����� � ������
volatile signed int		Dig_Ind_Avr = 0;												// ����� � ������ (����������� ��������)
volatile unsigned char	Sym_Razr_1 = Sym_3def;											// ������ ��� ��������� ������ 1
volatile unsigned char	Sym_Razr_2 = Sym_3def;											// ������ ��� ��������� ������ 2
volatile unsigned char	Sym_Razr_3 = Sym_3def;											// ������ ��� ��������� ������ 3
volatile unsigned char	Curr_Razr =	1;													// ������� ������ ��� �����������
volatile unsigned char	CountTime;														// ������� (���������������� �� ������� 0)
volatile unsigned char	Arr_Average_Position = 0;										// ��������� �� ������� ������� ����������
unsigned char Symbol [17] = {63, 6, 91, 79, 102, 109, 125, 7, 127, 111, 64, 113, 62, 56, 84, 0, 73};		//DOT - 128
int Arr_Average [Average_factor];

void Presets(void);
void SendTrig(void);
void SetSymbols(void);
void ChangeMode(void);
int Averaging(int);


	/**********************************************************************************************
	*                                     �������� �������                                        *
	**********************************************************************************************/

int main(void)
{
	Presets();
	
	while (1)
	{
		if (Flag_byte & (1<<_echo_ended))												// ���� ���������� ����������� ������������
		{
			if (EchoTimerCount <= 460)													// ������ �� �������� � ���� ���������� (��. ���� .xlsx)
				Dig_Ind = 182 - EchoTimerCount * 182 / 1620;
			else if (EchoTimerCount >= 1260)	
				Dig_Ind = 162 - EchoTimerCount * 162 / 1495;
			else								
				Dig_Ind = 190 - EchoTimerCount * 190 / 1455;			
			
			Dig_Ind_Avr = Averaging(Dig_Ind);											// ���������� ���������� ����������� ��������� (���������� �������)

			SetSymbols();																// ���������� ����������� �������� �� �������� ����������
			Flag_byte &=~ (1<<_echo_ended);												// ����� ����� �� ��������� ���������� ������
		}
		
		if ((Flag_byte & (1<<_btn_pressed)) && (CountTime >= Count_max_btn))			// ������ ������ � ��������� �����������
		{
			ChangeMode();																// ����� ������
			Flag_byte &=~ (1<<_btn_pressed);											// ����� ����� ������� ������
			GIMSK |= (1<<INT1);															// ���������� ���������� INT1 (�������� ����. �������)
		}
		
		asm("nop");
		
		#ifdef PROTEUS
			if ((!(Flag_byte & (1<<_mode_on))) && (!(Flag_byte & (1<<_mode_off))))
			if (CountTime > 80)															// ����� �� ������ Count_max_btn
			{
				SendTrig();
				CountTime = 0;
			}
		#endif
	}
}


	/**********************************************************************************************
	*                                    ��������� �������                                        *
	**********************************************************************************************/

		/*-------------------------------- ������������� ------------------------------------*/

void Presets(void)
{
	DDRB = 0xff;																	// ��������� ������
	DDRD |= (1<<Razr_1) | (1<<Razr_2) | (1<<Razr_3) | (1<<Pump);
	DDRSR04 |= (1<<SR04_trig);
	PORTB = 0;
	Razr_1_OFF; Razr_2_OFF; Razr_3_OFF;
	PORTSR04 &=~ (1<<SR04_trig);
	Pump_ON;
	PORTD |= (1<<BTN);
																					// ������ 0
	TCCR0A |= (1<<WGM01);																// ����� ������ ������� 0 (�. 11.8 ���. 84)		CTC
	OCR0A = 51;																			// �������� �������� �������� (11.9.4 ���. 75)	15625/(51+1)=300Hz  (3.3 ms)
	TCCR0B |= (1<<CS02);																// ������������ ������� 0 (�. 11.9 ���. 86)		4000000/256=15625 Hz
	TIMSK |= (1<<OCIE0A);																// ���������� ��� ���������� �
																					// ������ 1
	TCCR1B|= (1<<CS11);																	// ������������ ������� 1 (�. 12.6 ���. 114)	4000000/8=500000 Hz (2 us)
																					// ���������� �� ������ ������
	MCUCR |= (1<<ISC11);																// �� ������������ ������
//////////////////////////////////////////////////////////////////////////
//	GIMSK |= (1<<INT1);																	// ���������� ���������� INT1
	#ifndef PROTEUS																	// ������ WDT
		asm ("cli");																	// ���������� ������ ���������� �� ����� ���������������� WDT
		asm ("wdr");																	// ����� �������� WDT
		WDTCR |= (1<<WDCE) | (1<<WDE);
		WDTCR |= (1<<WDE) | (1<<WDP2) | (1<<WDP1) | (1<<WDP0);							// ������������ 256k - 2 ���. (����������� � ChangeMode()) (���. 47)
		WDTCR |= (1<<WDIE);																// ��������� WDT � ������ ����������
	#endif
	asm("sei");
}


        /*------------------------ �������� ������� �� ��������� ----------------------------*/
        
void SendTrig(void)
{
	PORTSR04 |= (1<<SR04_trig);															// ������ ��������
	for (unsigned char temp=0; temp<13; temp++)	asm("nop");								// �������� (~54 ����� ~13 us)
	PORTSR04 &=~ (1<<SR04_trig);														// ����� ��������
	        
	MCUCR |= (1<<ISC01) | (1<<ISC00);													// ���������� �� ������������� ������ INT0 (�������� ������ �������� Echo)
	GIMSK |= (1<<INT0);																	// ���������� ���������� INT0
}

        /*----------------------------- ���������� �������� ---------------------------------*/    // ���������� �������
        
int Averaging(int Dig)
{
	Arr_Average[Arr_Average_Position] = Dig;											// ������ � ������� ������� ������� ������ ��������
	Arr_Average_Position = (Arr_Average_Position + 1) % Average_factor;					// ������� ��������� �� ��������� ������� �������
	
	if ((Flag_byte & (1<<_array_full)) == 0)											// ���� ������ ������
	{
		for (unsigned char a=1; a < Average_factor; a++)								// ���������� ������� ����������� ����������
		{
			Arr_Average [a] = Dig;
		}
		
		Flag_byte |= (1<<_array_full);													// ��������� ����� ������������ �������
	}
	
	Dig = 0;
	for (unsigned char i=0; i < Average_factor; i++)
	{
		Dig = Dig + Arr_Average[i];														// ������������ ���� ��������� �������
	}
	return Dig / Average_factor;														// ��������� --> ����� ���� ��������� / ���������� ��������
	//	return Dig >> 3;																// ����� ����� ������� �� ������� ������ Average_Factor
}

        /*----------------------- ���������� ����� �� ��� ������� ---------------------------*/
        
void SetSymbols(void)
{
	asm("cli");
	
	Sym_Razr_1 = 0;	Sym_Razr_2 = 0; Sym_Razr_3 = 0;										// ����� ���������� ��������
	
	if (Dig_Ind_Avr <= 0)																// ���� ������� ���� ���� 0
	{
		Sym_Razr_1 = Sym_Defis;
		Sym_Razr_2 = Sym_Defis;
		Sym_Razr_3 = Sym_Defis;
		if (!(Flag_byte & (1<<_pump_is_off)))											// ���� ����� �������
		{
			Pump_OFF;																	// ���������� ������
			Flag_byte |= (1<<_pump_is_off);												// ��������� ����� ������������ ������
		}
	}
	else if (Dig_Ind_Avr > Level_max)													// ���� ������� ���� > max
	{
		Sym_Razr_1 = Sym_F;
		Sym_Razr_2 = Sym_U;
		Sym_Razr_3 = Sym_L;
	}
	else																				// ���� ������� � �������� �����
	{
		if (Flag_byte & (1<<_pump_is_off))												// ���� ����� ��������
		{
			if (Dig_Ind_Avr >= Level_pump_on)											// ���� ������� ������ ������ ���������
			{
				Pump_ON;																// ��������� ������
				Flag_byte &=~ (1<<_pump_is_off);										// ����� �����
			}
		}
		
		while (Dig_Ind_Avr >= 100)														// ���������� �� �����, �������, �������
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

        /*----------------------------- ����� ������ ������ ---------------------------------*/
        
void ChangeMode(void)
{
	if ((!(Flag_byte & (1<<_mode_on))) && (!(Flag_byte & (1<<_mode_off))))		// ���� ������� ����� AUTO
	{
		asm ("cli");
		asm ("wdr");
		WDTCR |= (1<<WDCE) | (1<<WDE);
		WDTCR = 0;																		// ����. ��������� ���������� (WDT)
		Flag_byte |= (1<<_mode_on);														// ���. ������ ON
		Pump_ON;
		Flag_byte &=~ (1<<_pump_is_off);												// ���. ������
		Sym_Razr_1 = 0;
		Sym_Razr_2 = Sym_n;
		Sym_Razr_3 = Sym_NUL;															// ��������� "On "
		asm("sei");
	} 
	else if (Flag_byte & (1<<_mode_on))											// ���� ������� ����� ON
	{
		asm ("cli");
		Flag_byte &=~ (1<<_mode_on);													// ����. ������ ON
		Flag_byte |= (1<<_mode_off);													// ���. ������ OFF
		Pump_OFF;
		Flag_byte |= (1<<_pump_is_off);													// ����. ������
		Sym_Razr_1 = 0;
		Sym_Razr_2 = Sym_F;
		Sym_Razr_3 = Sym_F;																// ��������� "OFF"
		asm("sei");
	}
	else if (Flag_byte & (1<<_mode_off))										// ���� ������� ����� OFF
	{
		asm("cli");
		Flag_byte &=~ (1<<_mode_off);													// ����. ������ OFF
		#ifndef PROTEUS
			asm ("wdr");
			WDTCR |= (1<<WDCE) | (1<<WDE);
			WDTCR |= (1<<WDE) | (1<<WDP2)| (1<<WDP1)| (1<<WDP0);
			WDTCR |= (1<<WDIE);															// ���. ��������� ���������� (WDT)
		#endif
		asm("sei");
		SendTrig();																		// ��������� �� ��������� ������������ WDT
	}
}


	/**********************************************************************************************
	*                                         ����������                                          *
	**********************************************************************************************/

		/*------------------------------ �� ���������� �������� -----------------------------*/

ISR (TIMER0_COMPA_vect)																	// 3.3 ms
{
	switch (Curr_Razr)
	{
		case 1:
			Razr_2_OFF; Razr_3_OFF; 
			PORTB = Symbol [Sym_Razr_1];
			Curr_Razr ++;
			if ((!(Flag_byte & 1<<_mode_on)) && (!(Flag_byte & 1<<_mode_off)) && (Sym_Razr_1 == 0)) break;	// ���������� ����������� ���� � ������ AUTO
			Razr_1_ON;
			break;
		case 2:
			Razr_1_OFF; Razr_3_OFF;
			PORTB = Symbol [Sym_Razr_2];
			Curr_Razr ++;
			if ((Sym_Razr_1 == 0) && (Sym_Razr_2 == 0)) break;							// ���������� ����������� ����
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

		/*------------------------------- �� ������� Watchdog -------------------------------*/
#ifndef PROTEUS
ISR (WDT_OVERFLOW_vect)																	// 2.0 s
{
	SendTrig();																			// �������� �������� �� Trig �������
	
	WDTCR |= (1<<WDIE);
}
#endif

		/*--------------------- ��� ����������� ������� � ������� SR04 ----------------------*/
        
ISR (INT0_vect)
{
	if ((Flag_byte & (1<<_echo_ongoing)) == 0)											// ���� Echo ������ ����� (_echo_ongoing == 0)
	{
		TCNT1 = 0;																		// ����� �������� �������
		Flag_byte |= (1<<_echo_ongoing);												// ������ �� ���� � ������ ����� Echo
		MCUCR &=~ (1<<ISC00);															// ���������� �� ���������� ������ INT0 (�������� ����� �������� Echo)
	}
	else
	{
		EchoTimerCount = TCNT1;															// ���������� ���������� ������ ��������
		Flag_byte &=~ (1<<_echo_ongoing);												// ������� ����� ������ ��������
		Flag_byte |= (1<<_echo_ended);													// ������ ����� �� ��������� ���������
		GIMSK &=~ (1<<INT0);															// ������ ���������� �� INT0
	}
}

		/*------------------------------- ��� ������� ������ --------------------------------*/
/*		
ISR (INT1_vect)
{
	GIMSK &=~ (1<<INT1);																// ������ ���������� INT1 (�� �����������, ���� ����������� �� ��������)
	CountTime = 0;																		// ����� ��������
	Flag_byte |= (1<<_btn_pressed);														// ��������� ����� ������� ������
}
*/

// 143 � 353 ������ ��������������� ��� ���������� ������ (����� �������) �� ��������� 
// ������ ������������ �� ������ ������