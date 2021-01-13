#include <Arduino.h>
#include <U8g2lib.h>
#include <string>
#include <SPI.h>
#include <data_stack.h>


//#define SERIAL_DEBUG
#define OLED

// The enabling and use of DWT CPU Clock Counter
#define DWTEn()         (*((uint32_t*)0xE000EDFC)) |= (1<<24)
#define CpuTicksEn()    (*((uint32_t*)0xE0001000)) = 0x40000001
#define CpuTicksDis()   (*((uint32_t*)0xE0001000)) = 0x40000000
#define CpuGetTicks()   (*((uint32_t*)0xE0001004))
U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ PA4, /* dc=*/ PA2, /* reset=*/ PA3);	// OLED lib 128 x 64 pixels

#define GATE1_PIN   PA1
#define GATE2_PIN   PA0
#define INVERT_LOGIC 0		//0 or 1
#define BTN_1 	PA13
#define BTN_2	PA14

class Projectile
{
public:
	Projectile() {}
	Projectile(float metres_per_sec, uint32_t time_seconds)	{
		velocity = metres_per_sec; 
		detection_time = time_seconds;
	}
	~Projectile() { }
	float velocity; 
	uint32_t detection_time = 0;
	//func
	float getFPS() {return velocity * 3.28084f;}
	float getMS() {return velocity;}
	float getEnergy(float weight_grams)
	{
		return 0.0005*weight_grams*velocity*velocity;
	}
	float deltaTimeSeconds(uint32_t t1, uint32_t t2)
	{
		return float(t2 - t1) * 13.888888888888889e-9;
	}
};

//Stack<Projectile, 20> Projectile_queue;
Stack<Projectile, 20> history;

uint32_t gate1_rising_time, gate2_rising_time, gate1_falling_time, gate2_falling_time, gate_reset_timer_ticks = 720000;	//clockticks = ms * 72e3 , here it is set to 10ms
bool gate1_triggered=0, gate2_triggered=0, shot_misaligned = 0;
float gate_distance_m	= 0.065;	
uint16_t valid_shot_count = 0;
//oled and menus
uint8_t oled_mode = 0, btn_1_previous, btn_2_previous;
uint32_t btn_1_time, btn_2_time, polling_time, poll_freq = 10, oled_time, oled_refresh_rate = 17;
uint16_t btn1_presses = 0, btn2_presses = 0; 
uint8_t row[4] = {11, 26, 40, 55};
bool speed_unit = 1, firerate_unit = 1; 



uint32_t rising1=0, rising2=0, falling1=0, falling2=0;
uint8_t enter1 = 0, enter2 = 0, left1 = 0, left2=0;

void displayNormal();
void displayMenu();
void displayStats();

void gate1_event()
{
	if (digitalRead(GATE1_PIN) == INVERT_LOGIC)		//pin is rising
	{
		#if defined(SERIAL_DEBUG)
			Serial.println("Enter 1");
		#endif
		shot_misaligned = 0;
		gate1_rising_time = CpuGetTicks();
		if (left1 == 0 && left2 == 0)	//make sure a full shot is complete before counting another one
		{
			rising1 = CpuGetTicks();
			enter1 = 1;
		}
	}
	else							// pin is falling
	{
		#if defined(SERIAL_DEBUG)
			Serial.println("Left 1");
		#endif
		gate1_falling_time = CpuGetTicks();
		gate1_triggered = 1;
		
		if (enter1 == 1)
		{
			falling1 = CpuGetTicks();
			left1 = 1;
			enter1 = 0;		//reset enter flag
		}
	}
}
void gate2_event()
{
	if (digitalRead(GATE2_PIN) == INVERT_LOGIC)		//rising
	{
		#if defined(SERIAL_DEBUG)
			Serial.println("Enter 2");
		#endif
		gate2_rising_time = CpuGetTicks();
		if (enter1 == 1 || left1 == 1 && left2 == 0)		//remove enter1==1 when the object fired is shorter than distance between diodes
		{
			rising2 = CpuGetTicks();
			enter2 = 1;
			left1 = 0;
		}
	}
	else			//falling
	{
		#if defined(SERIAL_DEBUG)
			Serial.println("Left 2 ");
			Serial.println("");
		#endif
		gate2_falling_time = CpuGetTicks();
		if (!gate1_triggered)		//if only the last gate was triggered, the BB must have missed. 
		{
			shot_misaligned = 1;
		}
		else 
			gate2_triggered = 1;
			

		if (enter2 == 1)
		{
			falling2 = CpuGetTicks();
			left2 = 1;
			enter2 = 0;
		}
	}
}


void handle_buttons()
{
	//check at a frequency (100hz) to reduce bouncing
	if (millis() - polling_time >= poll_freq )
	{
		polling_time = millis();
		//pushed
		if (!digitalRead(BTN_1) && !btn_1_previous)
		{
			btn_1_previous = 1;
			btn_1_time = millis();

		}	//released
		else if (digitalRead(BTN_1) && btn_1_previous)
		{
			btn_1_previous = 0;
			if (millis() - btn_1_time > 500)	//hold for 1.5secs at least
			{
				if (oled_mode == 2) oled_mode = 1;		//Settings Menu
				else oled_mode = 2;
				btn1_presses = 0;	btn2_presses = 0;
			}
			else 	//short click
			{
				btn1_presses ++;	
			}	
		}
		//Button DOWN
		if (!digitalRead(BTN_2) && !btn_2_previous)
		{
			btn_2_previous = 1;
			btn_2_time = millis();

		}	//released
		else if (digitalRead(BTN_2) && btn_2_previous)
		{
			btn_2_previous = 0;
			if (millis() - btn_2_time > 500)	//hold for 1.5secs at least
			{
				if (oled_mode == 3) oled_mode = 1;			//Stats
				else oled_mode = 3;
				btn1_presses = 0;	btn2_presses = 0;
			}
			else 	//short click
			{
				btn2_presses ++;	
			}	
		}
	}
}

/*  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~   SETUP ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~  */
void setup() {
	DWTEn();
	CpuTicksEn();
#if defined(SERIAL_DEBUG)
	Serial.begin(115200);
	delay(1500);
	Serial.println("Connected"); 
#endif
	//pin setups
	pinMode(GATE1_PIN, INPUT_PULLUP);
	pinMode(GATE2_PIN, INPUT_PULLUP);
	pinMode(BTN_1, INPUT_PULLUP);
	pinMode(BTN_2, INPUT_PULLUP);
	attachInterrupt(GATE1_PIN, gate1_event, CHANGE);
	attachInterrupt(GATE2_PIN, gate2_event, CHANGE);

#if defined(OLED)
	u8g2.begin();
	u8g2.firstPage();
	do {
		u8g2.setFont(u8g2_font_unifont_t_symbols);
		u8g2.setCursor(20,13);
		u8g2.print("Chronograph");
	} while (u8g2.nextPage());
#endif
	oled_mode = 1;

}


/*  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~   LOOP  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~  */
void loop() {
	//detect a valid shot
	//if (gate2_triggered && !shot_misaligned)
	if (left2 == 1)
	{
		if (oled_mode == 0)		oled_mode = 1;	//return to mainscreen to display BB info
		//take average of rising and falling times to get a more accurate delta time. 
		//float true_delta_time = float(gate2_rising_time + gate2_falling_time - gate1_rising_time - gate1_falling_time) * 13.888888888888889e-9 * 0.5;		//convert cpu clocks to sec
		float d1 = float(rising2 - rising1)  * 13.888888888888889e-9;
		float d2  = float(falling2 - falling1) * 13.888888888888889e-9;
		
		float velocity_ms = 2 * gate_distance_m / (d1 + d2 );
		uint32_t exit_time =   rising2 +(falling2 - rising2)/2 ;		//to be used for rounds per second calculation
		gate1_triggered = 0;		//reset flags
		gate2_triggered = 0;
		valid_shot_count++;
		
		Projectile *bb = new Projectile(velocity_ms, exit_time);
		history.push(*bb);
		delete bb;
		
		left2 = 0;
	}

	//if the second gate was never triggered, the shot was misaligned with the second gate
	//if (gate1_triggered && ( CpuGetTicks() - gate1_falling_time >= gate_reset_timer_ticks))
	//{
	//	gate1_triggered = 0;
	//	gate2_triggered = 0;
	//	shot_misaligned = 1;
	//}
	handle_buttons();
	if (millis() - oled_time > oled_refresh_rate)
	{
		oled_time = millis();
		switch (oled_mode)
		{
			case 1:		//primary screen, shows each fired shot
				displayNormal();
				break;
			case 2:
				displayMenu();
			break;
			case 3:
				displayStats();
			break;
			default:
				break;
		}
	}
	if (shot_misaligned && ((CpuGetTicks() -  gate2_falling_time ) * 13.888888888888889e-9 >= 0.3)) shot_misaligned =0;
}

void displayNormal()
{
	u8g2.firstPage();
	do {
		uint16_t select = btn1_presses % history.size();
		select = history.size() ? select : 0;
		u8g2.setCursor(0, row[0]);
		u8g2.print("Total Fired");
		u8g2.setCursor(100, row[0]);
		u8g2.print(valid_shot_count);

		u8g2.setCursor(0, row[1]);
		u8g2.print("History:");
		u8g2.setCursor(100, row[1]);
		u8g2.print(history.size() - select);

		u8g2.setCursor(100, row[2]);
		u8g2.print(speed_unit ? "fps" : "M/S");
		u8g2.setCursor(0, row[2]);
		if (history.size())
			u8g2.print( speed_unit ? history.get(select).getFPS() : history.get(select).getMS());
		u8g2.setCursor(100, row[3]);
		u8g2.print(firerate_unit ? "RPS" : "RPM");
		u8g2.setCursor(0, row[3]);
		if (history.get(select).velocity > 0 && history.get(select + 1).velocity > 0)
		{
			Projectile t; 
			u8g2.print( (firerate_unit ? 1 : 60) / t.deltaTimeSeconds(history.get(select).detection_time, history.get(select + 1).detection_time) );
		}
		else {
			u8g2.print("N/A");
		}

		if (shot_misaligned)
		{
			//u8g2.clear();
			u8g2.setCursor(0, row[0]);
			u8g2.print("MISSED");
		}

	} while (u8g2.nextPage());
}

void displayMenu()
{
	u8g2.firstPage();
	do {
		uint8_t available_options = 2;
		uint16_t select = (btn1_presses ) % available_options;
		u8g2.setCursor(0, row[0]);
		u8g2.print("Settings");

		u8g2.setCursor(10, row[1]);
		u8g2.print("Fire rate:");
		u8g2.setCursor(10, row[2]);
		u8g2.print("Speed:");
		u8g2.setCursor(100, row[1]);
		u8g2.print(firerate_unit ? "RPS" : "RPM");
		u8g2.setCursor(100, row[2]);
		u8g2.print(speed_unit ? "fps" : "M/S");
		//visible cursor
		u8g2.setCursor(0, row[1 + select]);
		u8g2.print(">");
		switch (select)
		{
			case 0:
				firerate_unit = (btn2_presses % 2 );
				break;
			case 1:
				speed_unit = (btn2_presses % 2 );
				break;
		} 

		u8g2.setCursor(0, row[3]);
		u8g2.print("Hold key to Exit");

	} while (u8g2.nextPage());
}

void displayStats()
{
	
	u8g2.firstPage();
	do {
		u8g2.setCursor(0, row[0]);
		u8g2.print("Stats");
		float min_vel = 99999, max_vel = -1, avg_vel  = 0, vel = 0;
		for (int i =0 ; i < history.size(); i ++ )
		{
			vel = history.get(i).velocity;
			if (vel > max_vel) max_vel = vel;
			if (vel< min_vel ) min_vel = vel;
			avg_vel += vel;
		}
		avg_vel /= history.size();
		u8g2.setCursor(0, row[1]);
		u8g2.print("Min");
		u8g2.setCursor(0, row[2]);
		u8g2.print("Average");
		u8g2.setCursor(0, row[3]);
		u8g2.print("MAX");
		u8g2.setCursor(70, row[1]);
		u8g2.print(speed_unit ? min_vel * 3.28084f: min_vel);
		u8g2.setCursor(70, row[2]);
		u8g2.print(speed_unit ? avg_vel * 3.28084f: avg_vel);
		u8g2.setCursor(70, row[3]);
		u8g2.print(speed_unit ? max_vel * 3.28084f: max_vel);

	} while (u8g2.nextPage());
}