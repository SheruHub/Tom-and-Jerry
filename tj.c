#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <avr/io.h> 
#include <avr/interrupt.h>
#include <util/delay.h>
#include <cpu_speed.h>
#include <string.h>

#include <graphics.h>
#include <macros.h>
#include "lcd_model.h"
#include "lcd.h"
#include "usb_serial.h"
#include "cab202_adc.h"


#define FREQ      (8000000.0)
#define PRESCALE0 (1.0) //  for a Freq of 7.8125Khz
#define PRESCALE1 (8.0)    //  for a Freq of 1Mhz
#define PRESCALE3 (1024.0)  //  for a Freq of 31.25Khz
#define SQRT(x,y) sqrt(x*x + y*y)
#define NUMELEMS(x)  (sizeof(x) / sizeof((x)[0]))
#define MAX(x,y) (x > y) ? x : y
#define MIN(x,y) (x < y) ? x : y
#define scale_velocity(x) (x*(duty_cycle_l/100)+0.1);			

#ifndef M_PI
#define M_PI        3.14159265358979323846264338327950288   /* pi             */
#endif

#define BACKLIGHT 1 // 1 for on, 0 for off

// Limits
#define GAME_CEILING 10
#define MAX_WALLS 6
#define MAX_CHEESE 5
#define MAX_TRAPS 5
#define MAX_FIREWORKS 20
#define MAX_CHAR_WIDTH 5 // max px width of Tom or Jerry
#define MAX_CHAR_HEIGHT 7 // max px height of Tom or Jerry
#define TN_OBJ_WIDTH 1 // tiny object
#define TN_OBJ_HEIGHT 1
#define SM_OBJ_WIDTH 3 // small object
#define SM_OBJ_HEIGHT 3
#define MD_OBJ_WIDTH 5 // medium object
#define MD_OBJ_HEIGHT 5
#define DEBOUNCE_MASK 0b00000011 // How many consequtive polls to assume input (debounce)
#define PURSUIT_DELAY   1 // how many ticks to predict

typedef enum { BT, LR } collide_dir; // bottom-top, left-right
typedef enum { WELCOME, RUNNING, PAUSE, GAMEOVER } GAME_STATE; 

// Gameplay variables
#define JERRY_SPEED 1
#define TOM_SPEED 0.8
#define FW_SPEED 2.5

// Holds a coordinate
typedef struct {
    float x, y;
} Coord;

// Any static object
typedef struct {
	int active; // active/inactive
	Coord pos; // position
	int w;
	int h;
	uint8_t* bitmap;
} Object;

// Any mobile object
typedef struct {
	Object obj;
	Coord origin;
    Coord d; // dx, dy
    float speed;
} Mobile;

// Wall data
typedef struct {
	Mobile data;
	int line[4]; // x1, y1, x2, y2
} Wall;

// Player data (or npc)
typedef struct {
    Mobile data;
    int droppable;
    int lives;
    int score;
} Player;

// Game state
typedef struct {
	Wall walls[MAX_WALLS];
	Object cheese[MAX_CHEESE];
	Object traps[MAX_TRAPS];
	Mobile fireworks[MAX_FIREWORKS];
	Object milk;
	Object door;
	int level;
	int cheese_count;
	int cheese_count_level;
	int cheese_timer;
	int trap_timer;
	int super_timer;
	int milk_timer;
	int super_mode;
	GAME_STATE state;
} Game;

GAME_STATE game_state;


// Tom bitmap
uint8_t tom_original[7] = {
    0b10001000,
	0b11011000,
	0b11111000,
	0b10101000,
	0b11111000,
	0b11111000,
	0b01110000,
};
uint8_t tom_direct[7];

// Jerry bitmap
uint8_t jerry_original[6] = {
	0b11011000,
	0b11011000,
	0b11011000,
	0b01110000,
	0b01110000,
	0b01110000,
};
uint8_t jerry_direct[6];

// Jerry super mode
uint8_t super_original[8] = {
	0b11001100,
	0b11111100,
	0b11111100,
	0b11111100,
	0b01111000,
	0b01111000,
	0b01111000,
	0b00110000,						
};
uint8_t super_direct[8];

// Cheese bitmap
uint8_t cheese_original[3] = {
	0b11100000,
	0b10000000,
	0b11100000,
};
uint8_t cheese_direct[3];

// Trap bitmap
uint8_t trap_original[3] = {
	0b10000000,
	0b01000000,
	0b11100000,
};
uint8_t trap_direct[3];

// Milk bitmap
uint8_t milk_original[3] = {
	0b01001000,
	0b11111100,
	0b10000100,
};
uint8_t milk_direct[3];

// Door bitmap
uint8_t door_original[5] = {
	0b11111000,
	0b10001000,
	0b10001000,
	0b10001000,
	0b10001000,
};
uint8_t door_direct[5];

// Firework bitmap
uint8_t firework_original[1] = {
	0b10000000,
};
uint8_t firework_direct[1];

Player jerry;
Player tom;
Game game;

//	(f) Create a volatile global variable called bit_counter.
//	The variable should be an 8-bit unsigned integer. 
//	Initialise the variable to 0.
volatile uint8_t bc_j_up = 0;
volatile uint8_t bc_j_down = 0;
volatile uint8_t bc_j_left = 0;
volatile uint8_t bc_j_right = 0;
volatile uint8_t bc_j_center = 0;
volatile uint8_t bc_b_left = 0;
volatile uint8_t bc_b_right = 0;

//	(g) Define a volatile global 8-bit unsigned global variable 
//	called switch_state which will store the current state of the switch.
volatile uint8_t sw_j_up;
volatile uint8_t sw_j_down;
volatile uint8_t sw_j_left;
volatile uint8_t sw_j_right;
volatile uint8_t sw_j_center;
volatile uint8_t sw_b_left;
volatile uint8_t sw_b_right;

// timing variables
volatile uint8_t overflow_counter0 = 0;
volatile uint32_t overflow_counter1 = 0;
volatile uint32_t overflow_counter3 = 0;
volatile float time_sec=0;
volatile int time_min=0;

// other vars
int left_right_click, up_down_click;
volatile int duty_cycle_r=0;
volatile int duty_cycle_l=0;
char buffer[80];
int wall_num; // for loading walls via serial
int supertimer;

// -------------------------------------------------
// Helper functions.
// -------------------------------------------------
void draw_float(uint8_t x, uint8_t y, float value, colour_t colour) {
	snprintf(buffer, sizeof(buffer), "%f", value);
	draw_string(x, y, buffer, colour);
}


void draw_int(uint8_t x, uint8_t y, int value, colour_t colour) {
	snprintf(buffer, sizeof(buffer), "%d", value);
	draw_string(x, y, buffer, colour);
}

void draw_int16(uint8_t x, uint8_t y, uint16_t value, colour_t colour) {
	snprintf(buffer, sizeof(buffer), "%u", (unsigned int)value);
	draw_string(x, y, buffer, colour);
}

void draw_formatted(int x, int y, char * buffer, int buffer_size, const char * format, ...) {
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, buffer_size, format, args);
	draw_string(x, y, buffer, FG_COLOUR);
}

void draw_formatted_center(int y, char * buffer, int buffer_size, const char * format, ...) {
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, buffer_size, format, args);
	unsigned char l = 0, i = 0;
	while (buffer[i] != '\0') { l++; i++; }
	char x = 42-(l*5/2);	
	draw_string(x, y, buffer, FG_COLOUR);
}

void draw_centred( unsigned char y, char* string ) {
	unsigned char l = 0, i = 0;
	while (string[i] != '\0') { l++; i++; }
	char x = 42-(l*5/2);
	draw_string((x > 0) ? x : 0, y, string, FG_COLOUR);
}

int rand_range( int min, int max) {
   	return rand() % (min - max + 1) + min;
}

void usb_serial_send(char * message) {
	// Cast to avoid "error: pointer targets in passing argument 1 
	//	of 'usb_serial_write' differ in signedness"
	usb_serial_write((uint8_t *) message, strlen(message));
}

void usb_serial_read_string(char * message){
	int c = 0;
	int buffer_count=0;
      
    while (c != '\n'){
			c = usb_serial_getchar();
			if(c == -1) break;
			message[buffer_count]=c;
			buffer_count++;
	}
}

int randInRange(int min, int max) {
	int out = min + rand()%(max+1 - min);
	return out;
}

// make bounce directions random
void rand_direction(Mobile* mob, int x, int y) {
    float dir = rand() * M_PI * 2 / RAND_MAX;
	int num = randInRange(0, (JERRY_SPEED - TOM_SPEED)*10);
    
    float speed = TOM_SPEED + num/10;
    //if (step < 0.1) step = 0.1;

	if (x) mob->d.x = speed * cos(dir);
	if (y) mob->d.y = speed * sin(dir);
	mob->speed = speed;
}

/*
*	Range = 0 - 127
**/
void set_contrast(int c){
	if ( c > 127 ) c = 0;
	if (c < 0) c = 127;
	
	LCD_CMD( lcd_set_function, lcd_instr_extended );
	LCD_CMD( lcd_set_contrast, c );
	LCD_CMD( lcd_set_function, lcd_instr_basic );	
}

void fade_in() {
	for(int i=0; i < 64; i++) {
		set_contrast(i);
		_delay_ms(10);
	}	
}

void fade_out() {
	for(int i=64; i > 0; i--) {
		set_contrast(i);
		_delay_ms(10);
	}	
}

void start_timer3(){
	SET_BIT(TCCR3B,CS32);     //1 see table 14-6
	SET_BIT(TCCR3B,CS30);   //0	
}

void stop_timer3(){
	CLEAR_BIT(TCCR3B,CS32);     //1 see table 14-6
	CLEAR_BIT(TCCR3B,CS30);   //0	
}

void reset_timer3(){
	TCNT3 = 0;
}

int get_current_time(){

	time_sec = ( overflow_counter3 * 65535.0 + TCNT3 ) * PRESCALE3  / FREQ;
	
	if (time_sec >= 60.00) {
		overflow_counter3 = 0.0;
		time_min++;
        time_sec = 0.0;
	}

return (int)time_sec;
}


// Set switch/button state
int set_state(uint8_t mask, uint8_t bit_counter) {
	return (bit_counter == mask);
}

// Find direction between two points
float get_direction(float x1, float y1, float x2, float y2) {
    float x = x2 - x1;
    float y = y1 - y2;
    return atan2(y,x);
}

float pursuit(Mobile* pursuer, Mobile* target) {
  int pd = PURSUIT_DELAY;
  float future_pos = get_direction(pursuer->obj.pos.x, pursuer->obj.pos.y, target->obj.pos.x, target->obj.pos.y) + target->speed * pd;
  return future_pos;
} 

void update_pursuer(Mobile* pursuer, Mobile* target) {
  float future_pos = pursuit(pursuer, target);
  future_pos = trunc(future_pos);
  int velocity = trunc (pursuer->speed + future_pos);
  pursuer->obj.pos.x += velocity;
  pursuer->obj.pos.y += velocity;
}

void move_walls() {
	for(int i=0; i < MAX_WALLS; i++) {
		if(game.walls[i].data.obj.active == 1) {
			//first convert line to normalized unit vector
			float diff_x = game.walls[i].line[2] - game.walls[i].line[0];
			float diff_y = game.walls[i].line[3] - game.walls[i].line[1];
			float angle = atan2(diff_y, diff_x);
			angle += M_PI/2; // To find the perpendicular
			float dist_x = 1 * cos(angle);
			float dist_y = -1 * sin(angle);

			//  + (duty_cycle_r/100)
			game.walls[i].line[0] += dist_x+ (duty_cycle_r/100); // x1
			game.walls[i].line[1] += dist_y+ (duty_cycle_r/100); // y1
			game.walls[i].line[2] += dist_x+ (duty_cycle_r/100); // x2
			game.walls[i].line[3] += dist_y+ (duty_cycle_r/100); // y2

			if(game.walls[i].line[0] > LCD_X && game.walls[i].line[2] > LCD_X) {
				game.walls[i].line[0] -= LCD_X-1; 
				game.walls[i].line[2] -= LCD_X-1; 
			}
			if(game.walls[i].line[1] > LCD_Y && game.walls[i].line[3] > LCD_Y) {
				game.walls[i].line[1] -= LCD_Y+GAME_CEILING-1;
				game.walls[i].line[3] -= LCD_Y+GAME_CEILING-1;
			}

			// if(game.walls[i].line[0] < LCD_X && game.walls[i].line[2] < LCD_X) {
			// 	game.walls[i].line[0] += LCD_X+1; 
			// 	game.walls[i].line[2] += LCD_X+1; 
			// }
			// if(game.walls[i].line[1] < GAME_CEILING && game.walls[i].line[3] < GAME_CEILING) {
			// 	game.walls[i].line[1] += LCD_Y-GAME_CEILING+1;
			// 	game.walls[i].line[3] += LCD_Y-GAME_CEILING+1;
			// }			


		}
	}
}

// Use for main game timer
ISR(TIMER0_OVF_vect) {	

	if(game.super_mode == 1) {
		//int supertime = game.super_timer;
		if (overflow_counter0 < supertimer*3){
			SET_BIT(PORTB,3);
			SET_BIT(PORTB,2);
		}
		else{
			CLEAR_BIT(PORTB,3);
			CLEAR_BIT(PORTB,2);
		}

	} else {
		if(BIT_IS_SET(PORTB,3)) CLEAR_BIT(PORTB,3);
		if(BIT_IS_SET(PORTB,2)) CLEAR_BIT(PORTB,2);
	}
	

	overflow_counter0 ++;
}


// Process teens input state and de-bounce via interrupt
//		    If bc_n is equal to 0, then the switch has been observed 
//			to be open at least DEBOUNCE_MASK times in a row, so store 0 in switch_state, 
//			indicating that the switch should now be considered to be officially "closed".
ISR(TIMER1_OVF_vect) {
	overflow_counter1 ++;

    bc_j_up = ((bc_j_up << 1) & DEBOUNCE_MASK) | BIT_IS_SET(PIND, 1);
	bc_j_down = ((bc_j_down << 1) & DEBOUNCE_MASK) | BIT_IS_SET(PINB, 7);
	bc_j_left = ((bc_j_left << 1) & DEBOUNCE_MASK) | BIT_IS_SET(PINB, 1);
	bc_j_right = ((bc_j_right << 1) & DEBOUNCE_MASK) | BIT_IS_SET(PIND, 0);
	bc_j_center = ((bc_j_center << 1) & DEBOUNCE_MASK) | BIT_IS_SET(PINB, 0);
	bc_b_left = ((bc_b_left << 1) & DEBOUNCE_MASK) | BIT_IS_SET(PINF, 6);
	bc_b_right = ((bc_b_right << 1) & DEBOUNCE_MASK) | BIT_IS_SET(PINF, 5);

	sw_j_up = set_state(DEBOUNCE_MASK, bc_j_up);
	sw_j_down = set_state(DEBOUNCE_MASK, bc_j_down);
	sw_j_left = set_state(DEBOUNCE_MASK, bc_j_left);
	sw_j_right = set_state(DEBOUNCE_MASK, bc_j_right);
	sw_j_center = set_state(DEBOUNCE_MASK, bc_j_center);
	sw_b_left = set_state(DEBOUNCE_MASK, bc_b_left);
	sw_b_right = set_state(DEBOUNCE_MASK, bc_b_right);		
	
    //increments
    // 16 * 65536 = 1048576 micro seconds approx 1.04 sec
    // walls update and LED toggle at 1Hz
    // if (overflow_counter1 > 10){
	// 	dx = (dx + 1) ;//% (1/1); 
	// 	dy = (dy + 1) ;//% (1/1); 
	//     overflow_counter1=0;
	//     //PORTB^=(1<<2);
	//    }	

    //increments
    //16 * 65536 = 1048576 micro seconds approx 1.04 sec
    //walls update and LED toggle at 1Hz
    if (overflow_counter1 > 10 && game_state != PAUSE){
		move_walls();
	    overflow_counter1=0;
	    //PORTB^=(1<<2);
	   }	
}

ISR(TIMER3_OVF_vect) {
	overflow_counter3 ++;
}

void turnOnLed0( int led ) {
    //  (d) Set pin 2 of the Port B output register. No other pins should be 
    //  affected. 
    if(led == 1) SET_BIT(PORTB,2);
	if(led == 2) SET_BIT(PORTB,3);
}

void turnOffLed0( int led ) {
    //  (e) Clear pin 2 of the Port B output register. No other pins should be
    //  affected.
    if(led == 1) CLEAR_BIT(PORTB,2);
	if(led == 2) CLEAR_BIT(PORTB,3);
}

float CalcDistanceBetween2Points(int x1, int y1, int x2, int y2)
{
    return SQRT((x1-x2), (y1-y2));
}


// Modified version of draw_pixel from graphics.c
int check_wall(int obj_x, int obj_y) {
	int collided = 0;
	int x1, y1, x2, y2;

	for(int i=0; i < MAX_WALLS; i++) {
		x1 = game.walls[i].line[0];
		y1 = game.walls[i].line[1];
		x2 = game.walls[i].line[2];
		y2 = game.walls[i].line[3];

		if ( x1 == x2 ) {
			// Draw vertical line
			for ( int i = y1; (y2 > y1) ? i <= y2 : i >= y2; (y2 > y1) ? i++ : i-- ) {
				if(x1 == obj_x && i == obj_y) collided = 1;
			}
		}
		else if ( y1 == y2 ) {
			// Draw horizontal line
			for ( int i = x1; (x2 > x1) ? i <= x2 : i >= x2; (x2 > x1) ? i++ : i-- ) {
				if(i == obj_x && y1 == obj_y) collided = 1;
			}
		}
		else {
			//	Always draw from left to right, regardless of the order the endpoints are 
			//	presented.
			if ( x1 > x2 ) {
				int t = x1;
				x1 = x2;
				x2 = t;
				t = y1;
				y1 = y2;
				y2 = t;
			}

			// Get Bresenhaming...
			float dx = x2 - x1;
			float dy = y2 - y1;
			float err = 0.0;
			float derr = ABS(dy / dx);

			for ( int x = x1, y = y1; (dx > 0) ? x <= x2 : x >= x2; (dx > 0) ? x++ : x-- ) {
				if(x == obj_x && y == obj_y) collided = 1;
				err += derr;
				while ( err >= 0.5 && ((dy > 0) ? y <= y2 : y >= y2) ) {
					if(x == obj_x && y == obj_y) collided = 1;
					y += (dy > 0) - (dy < 0);
					err -= 1.0;
				}
			}
		}
	}
	return collided;
}

Coord make_random_coord() {
    Coord result;
	result.x = rand_range(0, LCD_X);
	result.y = rand_range(GAME_CEILING, LCD_Y);
    return result;
}

void find_clear(Object* obj) {
	int found = 1;

	while(found) {
		obj->pos = make_random_coord();
		int newX = round(obj->pos.x);
		int newY = round(obj->pos.y);

		// Make sure coords are in game bounds
		while(newX > LCD_X-obj->w) newX--;
		while(newY > LCD_Y-obj->h) newY--;		

		found=0;

		if(!found) {
			for(int x = newX; x < newX + obj->w; x++) {
				for(int y = newY; y < newY + obj->h; y++) {

					//Find cheese
					if(!found) {
						for (int i=0; i < MAX_CHEESE; i++ ) {
							if(game.cheese[i].active == 1) {
								int obj2x = round(game.cheese[i].pos.x);
								int obj2y = round(game.cheese[i].pos.y);
								if((x == obj2x) && (y == obj2y)) {
									found=1;
								} 
							}
						} 
					}

					// // Find traps
					if(!found) {
						for (int i=0; i < MAX_TRAPS; i++ ) {
							if(game.traps[i].active == 1) {
								int obj2x = round(game.traps[i].pos.x);
								int obj2y = round(game.traps[i].pos.y);
								if((x == obj2x) && (y == obj2y)) {
									found=1;
								} 
							}
						} 
					}

					// Find Tom
					if(!found) {
						if(x == round(tom.data.obj.pos.x) && y == round(tom.data.obj.pos.y)) {
							found=1;
						} 
					}

					// Find Jerry
					if(!found) {
						if(x == round(jerry.data.obj.pos.x) && y == round(jerry.data.obj.pos.y)) {
							found=1;
						} 
					}

					// Find Door
					if(!found) {
						if(game.door.active == 1) {
							if(x == round(game.door.pos.x) && y == round(game.door.pos.y)) {
								found=1;
							} 
						}
					}

					// Find Walls
					if(!found)
					for (int i=0; i < MAX_WALLS; i++ ) {
						if(check_wall(x, y)) {
							found=1;
						} 
					} 
				} 
			}
		}
	}
}

int collide_bitmap_wall(int x1, int y1, int x2, int y2, collide_dir dir) {
	int collided = 0;
	if(dir == BT) {
		for(int i = y1; i < y2; i++) {
			if(check_wall(x1, i)) collided = 1;
		}

	} else {
		for(int i = x1; i < x2; i++) {
			if(check_wall(i, y1)) collided = 1;
		}
	}
	return collided;
}

void draw_walls() {
	for(int i = 0; i < MAX_WALLS; i++) {
		if (game.walls[i].data.obj.active == 1) {
			draw_line(game.walls[i].line[0], game.walls[i].line[1], game.walls[i].line[2], game.walls[i].line[3], BG_COLOUR);
		}
	}
}

/*
**  Draw a bitmap directly to LCD.
**  (Notice: y-coordinate.)
*/
void draw_data(Object *obj, uint8_t bitmap[]) {

    // Visit each column of output bitmap
    for (int i = 0; i < obj->w; i++) {
        // Visit each row of output bitmap
        for (int j = 0; j < obj->h; j++) {
			if (BIT_VALUE(bitmap[i], j) == 1) 
				draw_pixel(round(obj->pos.x+i), round(obj->pos.y+j),FG_COLOUR);
        }
    }
	
}

/*
** Remove 1 entity from the screen (1 bank size)
*/
void erase_entity(Mobile mob, uint8_t bitmap[]) {

    // Visit each column of output bitmap
    for (int i = 0; i < mob.obj.w; i++) {
        // Visit each row of output bitmap
        for (int j = 0; j < mob.obj.h; j++) {

			if (BIT_VALUE(bitmap[i], j) == 1) 
				draw_pixel(mob.obj.pos.x+i, mob.obj.pos.y+j,BG_COLOUR);
        }
    }	
}

void create_firework() {
	if (game.cheese_count >= 3) {
		for (int i = 0; i < MAX_FIREWORKS; i++) {
			if (game.fireworks[i].obj.active == 0) {
				game.fireworks[i].obj.active = 1;
				game.fireworks[i].obj.pos.x = jerry.data.obj.pos.x + jerry.data.obj.w/2;
				game.fireworks[i].obj.pos.y = jerry.data.obj.pos.y + jerry.data.obj.h/2;
				game.fireworks[i].speed=1;
				game.fireworks[i].obj.w = TN_OBJ_WIDTH;
				game.fireworks[i].obj.h = TN_OBJ_HEIGHT;
				game.fireworks[i].obj.bitmap=firework_direct;
				return;
			}
		}	
	}
}

/*
**	Setup walls
*/
void setup_walls_1() {
	game.walls[0].data.obj.active = 1;
	game.walls[0].line[0] = 18;
	game.walls[0].line[1] = 15;
	game.walls[0].line[2] = 13;
	game.walls[0].line[3] = 25;
	game.walls[0].data.obj.pos.x = 	game.walls[0].line[2] - game.walls[0].line[0];
	game.walls[0].data.obj.pos.y = 	game.walls[0].line[3] - game.walls[0].line[1];

	game.walls[1].data.obj.active = 1;
	game.walls[1].line[0] = 25;
	game.walls[1].line[1] = 35;
	game.walls[1].line[2] = 25;
	game.walls[1].line[3] = 45;
	game.walls[1].data.obj.pos.x = 	game.walls[1].line[2] - game.walls[1].line[0];
	game.walls[1].data.obj.pos.y = 	game.walls[1].line[3] - game.walls[1].line[1];	

	game.walls[2].data.obj.active = 1;
	game.walls[2].line[0] = 45;
	game.walls[2].line[1] = 10;
	game.walls[2].line[2] = 60;
	game.walls[2].line[3] = 10;
	game.walls[2].data.obj.pos.x = 	game.walls[2].line[2] - game.walls[2].line[0];
	game.walls[2].data.obj.pos.y = 	game.walls[2].line[3] - game.walls[2].line[1];	

	game.walls[3].data.obj.active = 1;
	game.walls[3].line[0] = 58;
	game.walls[3].line[1] = 25;
	game.walls[3].line[2] = 72;
	game.walls[3].line[3] = 30;
	game.walls[3].data.obj.pos.x = 	game.walls[3].line[2] - game.walls[3].line[0];
	game.walls[3].data.obj.pos.y = 	game.walls[3].line[3] - game.walls[3].line[1];	
}

/*
**	Define Tom 
*/
void setup_tom_1() {
	tom.data.origin.x = LCD_X - 6;
	tom.data.origin.y = LCD_Y - 9;
	tom.data.obj.pos = tom.data.origin;
	tom.data.speed = TOM_SPEED;
	tom.lives = 5;
	tom.data.obj.w = MAX_CHAR_WIDTH;
	tom.data.obj.h = MAX_CHAR_HEIGHT;
	tom.data.obj.bitmap = tom_direct;
	rand_direction(&tom.data, 1, 1);

}

/*
**	Define Jerry 
*/
void setup_jerry_1() {
	jerry.data.origin.x = 0;
	jerry.data.origin.y = 10;
	jerry.data.obj.pos = jerry.data.origin;
	jerry.data.speed = JERRY_SPEED;
	jerry.data.d.x = 1;
	jerry.data.d.y = 1;
	jerry.lives = 5;
	jerry.score = 0;
	jerry.data.obj.w = MAX_CHAR_WIDTH;
	jerry.data.obj.h = MAX_CHAR_HEIGHT;
	jerry.data.obj.bitmap = jerry_direct;
    
}

void reset_game_vars() {
	game.level=1;
	game.cheese_timer = 0;
	game.trap_timer = 0;
	game.milk_timer = 0;
	game.super_timer = 0;
	game.cheese_count = 0;
	game.cheese_count_level = 0;
	game.super_mode=0;
	time_min = 0;
}

void reset_objects() {
	// cheese
	for(int i=0;i<MAX_CHEESE;i++) game.cheese[i].active = 0;

	// traps
	for(int i=0;i<MAX_TRAPS;i++) game.traps[i].active=0;
	
	// fireworks
	for(int i=0;i<MAX_FIREWORKS;i++) game.fireworks[i].obj.active=0;	

	// door
	game.door.active=0;

	// milk
	game.milk.active=0;

	// Level cheese count
	game.cheese_count_level = 0;
}

void reset_game() {
	if(game_state==GAMEOVER || game_state == WELCOME) {
		setup_tom_1();
		setup_jerry_1();
		setup_walls_1();
		reset_game_vars();
		reset_timer3();
		game_state=RUNNING;	
	}
	reset_objects();
	fade_in();
}


void load_room(void){

	for (int i = 0; i < MAX_WALLS; i++) {
		game.walls[i].data.obj.active = 0;
	}
	draw_string(10, 10, "Connect USB...", FG_COLOUR);
	show_screen();

	// Setup USB
	usb_init();
	while ( !usb_configured() || !usb_serial_get_control()) {
	}	
	clear_screen();
	draw_string(10, 10, "USB connected", FG_COLOUR);
	show_screen();
	_delay_ms(2000);

	if (usb_serial_available()){
		char tx_buffer[32];

		int c = 0;
		
		while (c != -1) {
			c = usb_serial_getchar(); //read usb port

			if(c =='T'){ //
				usb_serial_read_string(tx_buffer);
				usb_serial_send( tx_buffer );
				sscanf( tx_buffer, "%f %f", &tom.data.obj.pos.x, &tom.data.obj.pos.y);
				tom.data.origin = tom.data.obj.pos; // Set Tom's original position
			}

			if(c =='J'){ //
				usb_serial_read_string(tx_buffer);
				usb_serial_send( tx_buffer );
				sscanf( tx_buffer, "%f %f", &jerry.data.obj.pos.x, &jerry.data.obj.pos.y);		
				jerry.data.origin = jerry.data.obj.pos;
			}

			//things to check here. Variable wall_num should be less than MAX_WALLS 
			if(c =='W'){
				char walls[64];
				usb_serial_read_string(walls);
				usb_serial_send( walls ); 
				if(wall_num > MAX_WALLS) {
					sprintf(tx_buffer,"Error: Too many walls");
					usb_serial_send( tx_buffer );
				} else {
					sscanf( walls, "%d %d %d %d", &game.walls[wall_num].line[0], &game.walls[wall_num].line[1], &game.walls[wall_num].line[2], &game.walls[wall_num].line[3]);
					game.walls[wall_num].data.obj.active = 1;
					wall_num++;				
				}

			}
		}
    }
}

void process_input(void) {
	static uint8_t prev_j_up = 0;
	static uint8_t prev_j_down = 0;
	static uint8_t prev_j_left = 0;
	static uint8_t prev_j_right = 0;
	static uint8_t prev_b_left = 0;
	static uint8_t prev_b_right = 0;
	static uint8_t prev_j_center = 0;

	int c = 0;

	if(game.level == 2) {
		if (usb_serial_available()){	
			c = usb_serial_getchar(); // read usb port
			usb_serial_flush_input(); // Kind of like a serial debouncer
		}
	}

	float jerryX = jerry.data.obj.pos.x;
	float jerryY = jerry.data.obj.pos.y;
	int jerryW = jerry.data.obj.w;
	int jerryH = jerry.data.obj.h;

	int newX, newY;
	int modX1, modX2, modY1, modY2;
	collide_dir collideDir;

	// direction vectors per joystick direction
	const Coord dirs[] = { {-1, 0}, {0, -1}, {1, 0}, {0, 1} }; // L, U, R, D
	int dir = -1;

	if (sw_j_up != prev_j_up || c == 'w' || c == 'W') 
		dir = 1;
	else if (sw_j_down != prev_j_down || c == 's' || c == 'S') 
		dir = 3;
	else if (sw_j_left != prev_j_left || c == 'a' || c == 'A') 
		dir = 0;
	else if (sw_j_right != prev_j_right || c == 'd' || c == 'D') 
		dir = 2;

	if (dir > -1)
	{
		newX = round(jerryX + dirs[dir].x);
		newY = round(jerryY + dirs[dir].y);
		modX1 = 0;
		modX2 = 0;
		modY1 = 0;
		modY2 = 0;

		if (dir == 1) // U
		{
			modX2 = jerryW;
			collideDir = LR;
		}
		if (dir == 3) { // D
			modX2 = jerryW;
			modY1 = jerryH-2;
			modY2 = jerryH-2;
			collideDir = LR;
		}
		if (dir == 0) { // L
			modY2 = jerryH-1;
			collideDir = BT;
		}
		if (dir == 2) { // R
			modY2 = jerryH-1;
			modX1 = jerryW-1;
			modX2 = jerryW-1;
			collideDir = BT;			
		}

		if(!collide_bitmap_wall(newX + modX1, newY + modY1, newX + modX2, newY + modY2, collideDir) || game.super_mode == 1) 
		{
			
			if (collideDir == LR)
			{
				jerry.data.obj.pos.y += scale_velocity(dirs[dir].y * jerry.data.speed);
				if (jerry.data.obj.pos.y > LCD_Y - MAX_CHAR_HEIGHT) jerry.data.obj.pos.y = LCD_Y - MAX_CHAR_HEIGHT;
				if (jerry.data.obj.pos.y < GAME_CEILING) jerry.data.obj.pos.y = GAME_CEILING;
			}
			else if (collideDir == BT)
			{
				jerry.data.obj.pos.x += scale_velocity(dirs[dir].x * jerry.data.speed);
				if (jerry.data.obj.pos.x < 0) jerry.data.obj.pos.x = 0;
				if (jerry.data.obj.pos.x > LCD_X - (MAX_CHAR_WIDTH)) jerry.data.obj.pos.x = LCD_X-(MAX_CHAR_WIDTH);
			}
		}
	}


	// JOYSTICK CENTER
	if (sw_j_center != prev_j_center || c == 'f') {
		create_firework();
		_delay_ms(100);
	}

	// LEFT BUTTON
	if (sw_b_left != prev_b_left || c == 'l') {
		//turnOnLed0();
		if(game.level == 2) {
			game_state = GAMEOVER;
			game.level=1;
			reset_game();

		} else {
			game.level=2;
			reset_game();
			load_room();
		}
	}

	// RIGHT BUTTON
	if (sw_b_right != prev_b_right || c == 'p') {
		//turnOffLed0();
		if(game_state == RUNNING) {
			game_state = PAUSE;
			stop_timer3();
		}
		else {
			game_state = RUNNING;
			start_timer3();
		}
	}		
}


/*
**  Convert bitmap into vertical slices for direct drawing.
*/
void setup_bitmap(int cols, int rows, uint8_t original[], uint8_t direct[]) {
    // Visit each column of output bitmap
    for (int i = 0; i < cols; i++) {
        // Visit each row of output bitmap
        for (int j = 0; j < rows; j++) {
            // Flip about the major diagonal.
            uint8_t bit_val = BIT_VALUE(original[j], cols-1-i);
            WRITE_BIT(direct[i], j, bit_val);
        }
    }
}


/*
**	Setup all bitmaps
*/
void setup_bitmaps() {
	setup_bitmap(8,NUMELEMS(milk_original),milk_original,milk_direct);	
	setup_bitmap(8,NUMELEMS(jerry_original),jerry_original,jerry_direct);	
	setup_bitmap(8,NUMELEMS(cheese_original),cheese_original,cheese_direct);	
	setup_bitmap(8,NUMELEMS(tom_original),tom_original,tom_direct);	
	setup_bitmap(8,NUMELEMS(trap_original),trap_original,trap_direct);	
	setup_bitmap(8,NUMELEMS(door_original),door_original,door_direct);
	setup_bitmap(8,NUMELEMS(firework_original),firework_original,firework_direct);
	setup_bitmap(8,NUMELEMS(super_original),super_original,super_direct);
}


/*
**	Test only the overlapping part of the bitmaps
**  Narrow phase collisions. This can be shortened a lot
**  Written this way for readability
*/
int collide_bitmaps(Object* a, Object* b) {

	// Find overlapping pixel grid
	int bNew = MIN((int)a->pos.y + a->h, (int)b->pos.y + b->h); // new bottom
	int tNew = MAX((int)a->pos.y, (int)b->pos.y); // new top
	int lNew = MAX((int)a->pos.x, (int)b->pos.x); // new left
	int rNew = MIN((int)a->pos.x + a->w, (int)b->pos.x + b->w); // new right

	// starting rows and columns in bitmap
	int aRow = (int)tNew - (int)a->pos.y;
	int aCol = (int)lNew - (int)a->pos.x;
	int bRow = (int)tNew - (int)b->pos.y;
	int bCol = (int)lNew - (int)b->pos.x;			

	// Number of rows & cols in the overlapping grid
	int numCols = (int)rNew - (int)lNew;
	int numRows = (int)bNew - (int)tNew;

	for(int rows = 0; rows < numRows; rows++) {
		for (int cols = 0; cols < numCols; cols++) {
			if (BIT_IS_SET(a->bitmap[aCol], aRow) && BIT_IS_SET(b->bitmap[bCol], bRow)) {
				return 1; // Overlapping pixel found (collision)
			}	
			aCol++; // Increment bitmap cols based on
			bCol++; // how far into the overlap grid we are
		}
		aRow++; // Increment bitmap rows based on
		bRow++; // how far into the overlap grid we are
		aCol -= numCols; // Reset cols on new row
		bCol -= numCols;
	}
	return 0;
}


// Broad phase collision 
int obj_collided(Object* a, Object* b) {
	int aB = a->pos.y + a->h-1; // a - bottom edge
	int aT = a->pos.y;		    // a - top edge
	int aL = a->pos.x;		    // a - left edge
	int aR = a->pos.x + a->w-1; // a - right edge

	int bB = b->pos.y + b->h-1;
	int bT = b->pos.y;
	int bL = b->pos.x;
	int bR = b->pos.x + b->w-1;

	// Can't be collision
	if (aB <= bT || aT > bB || aL > bR || aR < bL) return 0;

	return collide_bitmaps(a, b); // return narrow phase
}

void reset_position(Mobile* mob) {
	mob->obj.pos = mob->origin;
}

void make_super() {
	game.super_mode = 1;
	jerry.data.obj.bitmap = super_direct;
	jerry.data.obj.w = 6;
	jerry.data.obj.h = 8;
	game.super_timer = supertimer = get_current_time();
}

void clear_super() {
	game.super_mode = 0;
	jerry.data.obj.w = CHAR_WIDTH;
	jerry.data.obj.h = CHAR_HEIGHT;
	jerry.data.obj.bitmap = jerry_direct;
}

void  do_collisions() {

	// Jerry and Tom 
	if (obj_collided(&jerry.data.obj, &tom.data.obj)) {
		if(game.super_mode == 0) {
			jerry.lives--;
			reset_position(&jerry.data);
		} else jerry.score++;
		reset_position(&tom.data);
	}

	// Jerry and cheese
	for (int i = 0; i < MAX_CHEESE; i++) {
		if (game.cheese[i].active == 1 && obj_collided(&jerry.data.obj, &game.cheese[i])) {
			jerry.score++;
			game.cheese_count++;				
			game.cheese_count_level++;
			game.cheese[i].active = 0;
			game.cheese_timer = get_current_time();
		}
	}

	// Jerry and traps
	for (int i = 0; i < MAX_TRAPS; i++) {
		if (game.traps[i].active == 1 && obj_collided(&jerry.data.obj, &game.traps[i]) && game.super_mode == 0) {
			jerry.lives--;
			game.traps[i].active = 0;
			game.trap_timer = get_current_time(); 
		}
	}	

	// Jerry and door
	if(game.door.active == 1 && obj_collided(&jerry.data.obj, &game.door)) {
		//turnOnLed0(1);
		if(game.level == 1) {
			game.level = 2;
			reset_game();
			load_room();
		} else {
			game_state=GAMEOVER;
		}
	}

	// Jerry and milk
	if(game.milk.active == 1 && obj_collided(&jerry.data.obj, &game.milk)) {
		game.milk.active =0 ;
		make_super();
	}

	// Tom and firework
	for (int i = 0; i < MAX_FIREWORKS; i++) {
		if (game.fireworks[i].obj.active == 1 && obj_collided(&tom.data.obj, &game.fireworks[i].obj)) {
			jerry.score++;
			reset_position(&tom.data);
			// Clear fireworks
			for (int i = 0; i < MAX_FIREWORKS; i++) {
				game.fireworks[i].obj.active = 0;
			}
		}
	}

	// Game over if Jerry's dead
	if(jerry.lives == 0) game_state=GAMEOVER;
}

void move_tom() {

	int new_x = tom.data.obj.pos.x + tom.data.d.x;
	int new_y = tom.data.obj.pos.y + tom.data.d.y;	

	// Check horizontal game area bounds
	if(new_x < 0 || new_x > LCD_X-tom.data.obj.w) {
		rand_direction(&tom.data, 1, 0); // Randomise x-bounce direction
		new_x = tom.data.obj.pos.x + tom.data.d.x;
		if(new_x < 0 || new_x > LCD_X-tom.data.obj.w) tom.data.d.x = -tom.data.d.x;
	}

	// Check vertical game area bounds
	if(new_y < GAME_CEILING || new_y > LCD_Y - tom.data.obj.h) {
		rand_direction(&tom.data, 0, 1); // Randomise y-bounce direction
		new_y = tom.data.obj.pos.y + tom.data.d.y;
		if(new_y < GAME_CEILING || new_y > LCD_Y - tom.data.obj.h) tom.data.d.y = -tom.data.d.y;
	}

	// Check for walls on the right
	if(tom.data.d.x > 0) {
		if(collide_bitmap_wall(new_x + tom.data.obj.w-1, new_y, new_x + tom.data.obj.w-1, new_y + tom.data.obj.h-1, BT)) {
			rand_direction(&tom.data, 1, 0); // Randomise x-bounce direction
			new_x = tom.data.obj.pos.x + tom.data.d.x;
			if(collide_bitmap_wall(new_x + tom.data.obj.w-1, new_y, new_x + tom.data.obj.w-1, new_y + tom.data.obj.h-1, BT))
				tom.data.d.x = -tom.data.d.x;
		}
	}
	// Check for walls on the left	
	if(tom.data.d.x < 0) {
		if(collide_bitmap_wall(new_x, new_y, new_x, new_y + tom.data.obj.h-1, BT)) {
			rand_direction(&tom.data, 1, 0); // Randomise x-bounce direction
			new_x = tom.data.obj.pos.x + tom.data.d.x;
			if(collide_bitmap_wall(new_x, new_y, new_x, new_y + tom.data.obj.h-1, BT))
				tom.data.d.x = -tom.data.d.x;			
		}		
	}
	// Check for walls below	
	if(tom.data.d.y > 0) {
		if(collide_bitmap_wall(new_x, new_y + tom.data.obj.h-1, new_x + tom.data.obj.w-1, new_y + tom.data.obj.h-1, LR)) {
			rand_direction(&tom.data, 0, 1); // Randomise y-bounce direction
			new_y = tom.data.obj.pos.y + tom.data.d.y;
			if(collide_bitmap_wall(new_x, new_y + tom.data.obj.h-1, new_x + tom.data.obj.w-1, new_y + tom.data.obj.h-1, LR))
				tom.data.d.y = -tom.data.d.y;
		}	
	}
	// Check for walls above
	if(tom.data.d.y < 0) {
		if(collide_bitmap_wall(new_x, new_y, new_x + tom.data.obj.w-1, new_y, LR)) {
			rand_direction(&tom.data, 0, 1); // Randomise y-bounce direction
			new_y = tom.data.obj.pos.y + tom.data.d.y;
			if(collide_bitmap_wall(new_x, new_y, new_x + tom.data.obj.w-1, new_y, LR))
				tom.data.d.y = -tom.data.d.y;			
		}
	}

	// Move Tom
	tom.data.obj.pos.x += scale_velocity(tom.data.d.x);
	tom.data.obj.pos.y += scale_velocity(tom.data.d.y);
}

void move_fireworks() {
	for (int i = 0; i < MAX_FIREWORKS; i++) {
		if (game.fireworks[i].obj.active == 1) {

			// Direction from firework to tom
			float dir = get_direction(game.fireworks[i].obj.pos.x, game.fireworks[i].obj.pos.y, tom.data.obj.pos.x, tom.data.obj.pos.y);

			// Calc delta
			game.fireworks[i].d.x = FW_SPEED * cos(dir);
			game.fireworks[i].d.y = -FW_SPEED * sin(dir);            

			game.fireworks[i].obj.pos.x = game.fireworks[i].obj.pos.x + game.fireworks[i].d.x;
			game.fireworks[i].obj.pos.y = game.fireworks[i].obj.pos.y + game.fireworks[i].d.y;

			// Check for walls
			if(check_wall((int)game.fireworks[i].obj.pos.x, (int)game.fireworks[i].obj.pos.y)) {
				game.fireworks[i].obj.active = 0;
			}
		}

	}	
}

void process_traps() {
	if (get_current_time() - game.trap_timer >= 3) {

		for (int i = 0; i < MAX_TRAPS; i++) {
			if (game.traps[i].active == 0) {
				game.traps[i].active = 1;
				game.traps[i].pos.x = tom.data.obj.pos.x + tom.data.obj.w/2;
				game.traps[i].pos.y = tom.data.obj.pos.y + tom.data.obj.h/2;
				game.traps[i].w = SM_OBJ_WIDTH;
				game.traps[i].h = SM_OBJ_HEIGHT;
				game.traps[i].bitmap = trap_direct;
				game.trap_timer = get_current_time();
				return;
			}
		}
	}
}

void process_cheese() {
	if (get_current_time() - game.cheese_timer >= 2) {

		for (int i = 0; i < MAX_CHEESE; i++) {
			if (game.cheese[i].active == 0) {
				//game.cheese[i].active = 1;
				game.cheese[i].w = SM_OBJ_WIDTH;
				game.cheese[i].h = SM_OBJ_HEIGHT;
				find_clear(&game.cheese[i]);
				game.cheese[i].active=1;
				game.cheese[i].bitmap = cheese_direct;			
				game.cheese_timer = get_current_time();
				return;
			}
		}
	}
}

void draw_cheese() {
	process_cheese();

	for (int i = 0; i < MAX_CHEESE; i++) {
		if (game.cheese[i].active == 1) draw_data(&game.cheese[i],game.cheese[i].bitmap);
	}
}

void draw_traps() {
	process_traps();
	for (int i = 0; i < MAX_TRAPS; i++) {
		if (game.traps[i].active == 1) draw_data(&game.traps[i], game.traps[i].bitmap);
	}
}

void draw_fireworks() {
	for (int i = 0; i < MAX_FIREWORKS; i++) {
		if (game.fireworks[i].obj.active == 1) draw_data(&game.fireworks[i].obj, game.fireworks[i].obj.bitmap);
	}
}

void draw_door() {
	if(game.cheese_count_level == 5 && game.door.active == 0) {
		game.door.w = MD_OBJ_WIDTH;
		game.door.h = MD_OBJ_HEIGHT;
		find_clear(&game.door);		
		game.door.active = 1;		
		game.door.bitmap = door_direct;
	}
	if(game.door.active == 1) draw_data(&game.door, game.door.bitmap);

}

void draw_milk() {
	if(get_current_time() - game.milk_timer >= 5) {
		game.milk.w = 6;
		game.milk.h = SM_OBJ_HEIGHT;
		find_clear(&game.milk);		
		game.milk.active = 1;		
		game.milk.bitmap = milk_direct;
		game.milk_timer = get_current_time();
	}
	if(game.milk.active == 1) draw_data(&game.milk, game.milk.bitmap);
}

void check_super() {
	if(game.super_mode == 1 && get_current_time() > game.super_timer + 10) {
		game.super_mode = 0;
		jerry.data.obj.bitmap = jerry_direct;
		jerry.data.obj.w = CHAR_WIDTH;
		jerry.data.obj.h = CHAR_HEIGHT;
	}
	if(game.super_mode == 1) {
		supertimer = get_current_time() - game.super_timer;
	}
}

/*
** Draw the status bar
*/
void draw_status_bar(void) {

	int time = get_current_time();
	draw_formatted( 0, 1, buffer, sizeof(buffer), "L%d h%d s%d T%.2d:%.2d",game.level, jerry.lives,jerry.score, time_min,time );	

	// Draw separator line
	draw_line(0, GAME_CEILING-1, LCD_X, GAME_CEILING-1, BG_COLOUR);
}

void wait_sw_r(void) {
	static uint8_t prev_b_right = 0;	
	while (1){
		// if ((PINF>>5) & 0b1){
		// 	break;
		// }
		if(sw_b_right != prev_b_right) break;
	}	
}

void wait_sw_l(void) {
	static uint8_t prev_b_left = 0;	
	while (1){
		// if ((PINF>>6) & 0b1){
		// 	break;
		// }
		if(sw_b_left != prev_b_left) break;
	}	
}

void draw_welcome_screen() {
	clear_screen();
	draw_centred(3, "T&J's Quibble");
	draw_centred(LCD_Y / 4 + 2, "Sheru B");
	draw_centred(LCD_Y / 4 * 2 + 2, "n4383079");
	draw_centred(LCD_Y / 4 * 3 + 2, "Cont: R");
	
	show_screen();
	fade_in();
	wait_sw_r();
	fade_out();
	clear_screen();

	reset_game();
}

void draw_gameover_screen() {
	clear_screen();
	draw_centred(3, "GAME OVER");
	draw_formatted_center(LCD_Y / 4 + 2, buffer, sizeof(buffer), "Score: %d", jerry.score);
	draw_centred(LCD_Y / 4 * 3 + 2, "Restart: R");
	
	show_screen();
	fade_in();
	wait_sw_r();
	fade_out();
	clear_screen();

	reset_game();
}

/*
**  Initialise the LCD display.
*/
void new_lcd_init(uint8_t contrast) {
    // Set up the pins connected to the LCD as outputs
    SET_OUTPUT(DDRD, SCEPIN); // Chip select -- when low, tells LCD we're sending data
    SET_OUTPUT(DDRB, RSTPIN); // Chip Reset
    SET_OUTPUT(DDRB, DCPIN);  // Data / Command selector
    SET_OUTPUT(DDRB, DINPIN); // Data input to LCD
    SET_OUTPUT(DDRF, SCKPIN); // Clock input to LCD

    CLEAR_BIT(PORTB, RSTPIN); // Reset LCD
    SET_BIT(PORTD, SCEPIN);   // Tell LCD we're not sending data.
    SET_BIT(PORTB, RSTPIN);   // Stop resetting LCD

    LCD_CMD(lcd_set_function, lcd_instr_extended);
    LCD_CMD(lcd_set_contrast, contrast);
    LCD_CMD(lcd_set_temp_coeff, 0);
    LCD_CMD(lcd_set_bias, 3);

    LCD_CMD(lcd_set_function, lcd_instr_basic);
    LCD_CMD(lcd_set_display_mode, lcd_display_normal);
    LCD_CMD(lcd_set_x_addr, 0);
    LCD_CMD(lcd_set_y_addr, 0);
}


/*
** Timer0 used for game input interrupts
*/
//setup a 8 bit timer
void setup_timer0(void) {

		// Timer 0 in normal mode (WGM02), 
		// with pre-scaler 1024 ==> ~30Hz overflow 	(CS02,CS01,CS00).
		// Timer overflow on. (TOIE0)
		CLEAR_BIT(TCCR0B,WGM02);
		//prescaler 1
		
		CLEAR_BIT(TCCR0B,CS02);  //0
		CLEAR_BIT(TCCR0B,CS01); //0
		SET_BIT(TCCR0B,CS00);   //1
		
		//enabling the timer overflow interrupt
		SET_BIT(TIMSK0, TOIE0);

}

//setup a 16bit timer
void setup_timer1(void) {

		// Timer 1 in normal mode (WGM12. WGM13), 
		// with pre-scaler 8 ==> 	(CS12,CS11,CS10).
		// Timer overflow on. (TOIE1)

		CLEAR_BIT(TCCR1B,WGM12);
		CLEAR_BIT(TCCR1B,WGM13);
		CLEAR_BIT(TCCR1B,CS12);    //0 see table 14-6
		SET_BIT(TCCR1B,CS11);      //1
		CLEAR_BIT(TCCR1B,CS10);     //0
		
	    //enabling the timer overflow interrupt
		SET_BIT(TIMSK1, TOIE1);
		
}

//setup a 16bit timer
void setup_timer3(void) {

	   // Timer 3 in normal mode (WGM32. WGM33), 
		// with pre-scaler 256 ==> 	(CS32,CS31,CS30).
		// Timer overflow on. (TOIE3)

		CLEAR_BIT(TCCR3B,WGM32);
		CLEAR_BIT(TCCR3B,WGM33);
		SET_BIT(TCCR3B,CS32);     //1 see table 14-6
		CLEAR_BIT(TCCR3B,CS31);   //0
		SET_BIT(TCCR3B,CS30);   //0
		
		//enabling the timer overflow interrupt
		SET_BIT(TIMSK3, TOIE3);

}

void setup_controller() {
    //enable buttons/inputs
	DDRD &= ~(1 << 1); // joystick up
	DDRB &= ~(1 << 1); // joystick left
	DDRD &= ~(1 << 0); // joystick right
	DDRB &= ~(1 << 7); // joystick down
	DDRB &= ~(1 << 0); // joystick center

    DDRF &= ~(1 << 6); // SW1 - Left button
    DDRF &= ~(1 << 5); // SW2 - Right button

	DDRF &= ~(1 << 0); // ADC0 - Left wheel
	DDRF &= ~(1 << 1); // ADC1 - Right wheel
}

// Generate seed based on ADC values and time
uint8_t generateSeed() {
	int seed = 0;
	int left_adc = adc_read(0);
	int right_adc = adc_read(1);	
	duty_cycle_r = (int)254.0 * (right_adc/1023.0);
	duty_cycle_l = (int)254.0 * (left_adc/1023.0);
	seed = duty_cycle_r+duty_cycle_l;
	seed += overflow_counter0;
	seed += overflow_counter1;
	seed += overflow_counter3;
	return seed;
}


void setup(void) {
	set_clock_speed(CPU_8MHz);	
	setup_timer0(); // For inputs via interrupt
	setup_timer1(); // 
	setup_timer3(); // For game timer events
	
    sei(); //	(c) Turn on interrupts.

	setup_controller();

    //  (c) Turn off LED0 (and all other outputs connected to Port B) by 
    //  clearing all bits in the Port B output register.
	DDRB |= (1 << 2); // Set led2 for output	
	DDRB |= (1 << 3); // Set led1 for output	
	PORTB = 0;	

	if (BACKLIGHT) PORTC |= (1 << 7); // Turn on backlight if set

	new_lcd_init(LCD_DEFAULT_CONTRAST);

	clear_screen();

	//init ADC
	adc_init();	
	srand(generateSeed());	// Configures USB	
	setup_bitmaps();
	game_state=WELCOME;	
}

void process(void) {
	// int16_t char_code = usb_serial_getchar();

	// if ( char_code >= 0 ) {
	// 	snprintf( buffer, sizeof(buffer), "received '%c'\r\n", char_code );
	// 	usb_serial_send( buffer );
	// }
	clear_screen();	

	// turnOffLed0(1);
	// turnOffLed0(2);
	check_super();
	process_input();    
	if(game_state != PAUSE) move_tom();
	move_fireworks();	
	//move_walls();

	do_collisions();

	draw_walls();
	draw_cheese();
	draw_traps();
	draw_door();
	draw_fireworks();
	draw_milk();	

	draw_data(&tom.data.obj, tom.data.obj.bitmap);
	draw_data(&jerry.data.obj, jerry.data.obj.bitmap);
	
	if(game.level == 2) {
		char tx_buffer[32];
		sprintf(tx_buffer, "Time: %.2d:%.2d\n",time_min, get_current_time());
		usb_serial_send( tx_buffer );
		sprintf(tx_buffer, "Level: %d\n",game.level);
		usb_serial_send( tx_buffer );		
		sprintf(tx_buffer, "Score: %d\n",jerry.score);
		usb_serial_send( tx_buffer );
		int count=0;
		for(int i= 0; i < MAX_FIREWORKS; i++) {
			if(game.fireworks[i].obj.active == 1) count++;
		}
		sprintf(tx_buffer, "Fireworks: %d\n",count);
		usb_serial_send( tx_buffer );

		for(int i= 0; i < MAX_CHEESE; i++) {
			if(game.cheese[i].active == 1) count++;
		}		
		sprintf( tx_buffer, "Cheese: %d\n", count );		
		usb_serial_send( tx_buffer );

		for(int i= 0; i < MAX_TRAPS; i++) {
			if(game.traps[i].active == 1) count++;
		}		
		sprintf( tx_buffer, "Traps: %d\n", count );
		usb_serial_send( tx_buffer );

		sprintf( tx_buffer, "Cheese cur room: %d\n", game.cheese_count_level);
		usb_serial_send( tx_buffer);

		sprintf( tx_buffer, "Super mode: %d\n", game.super_mode);
		usb_serial_send(tx_buffer);

		int paused=0;
		if(game_state == PAUSE) paused = 1; 
		sprintf( tx_buffer, "Paused: %d\n", paused);
		usb_serial_send(tx_buffer);		
	}
	draw_status_bar();

	//duty cycle is adjusted with the potentiometer 
	int left_adc = adc_read(0);
	int right_adc = adc_read(1);
	duty_cycle_r = (int)254.0 * (right_adc/1023.0);
	duty_cycle_l = (int)254.0 * (left_adc/1023.0);

	//add logic to cycle the variable duty_cycle from 0 - TOP - 0
	// //that would gradually dim the LED

	// draw_formatted(15,24, buffer, sizeof(buffer), "%d", duty_cycle_r );	
	// draw_formatted(15,34, buffer, sizeof(buffer), "%d", duty_cycle_l );	

	show_screen();	
		
}

int main(void) {
	setup();

	for ( ;; ) {
		if(game_state == WELCOME) {
			draw_welcome_screen();
		} else if (game_state == RUNNING || game_state == PAUSE) {
			process();
		} else if (game_state == GAMEOVER) {
			draw_gameover_screen();
		}
	}
}