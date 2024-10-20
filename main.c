#include <avr/io.h>
#include <util/delay.h>
#include <string.h>
#include <avr/interrupt.h>
#include <stdbool.h>
#include <stdio.h>

//-----------------------------------------------------------------------------
uint8_t rcv_buff[84];
uint8_t rcv_index = 0;

// Effect Control Variables
uint16_t effect_dly = 100; // Initial delay for effects (speed)
uint8_t effect = 0;         // Effect selector (0: running_fire_l, 1: running_shadow, 2: johnson_counter, 3: blink_all, 4: binary_count)
uint8_t effect_running = 0; // 1 = effect running, 0 = effect stopped

// Temperature Sending Control Variables
bool temp_sending_active = false; // Flag for periodic temperature sending
uint16_t temp_delay_counter = 0;   // Counter for delay

// Timing Constants
#define TEMP_SEND_INTERVAL_MS 2000          // 2 seconds
#define LOOP_DELAY_MS 2                      // Main loop delay per iteration
#define TEMP_DELAY_COUNT (TEMP_SEND_INTERVAL_MS / LOOP_DELAY_MS) // 1000 iterations

// Shift Register Pin Definitions
#define HC595_DATA   PB3   // SER - Serial Data Input
#define HC595_SHCP   PB5   // SRCLK - Shift Clock
#define HC595_STCP   PB1   // RCLK - Storage Clock (Latch)
#define HC595_PORT   PORTB

// Thermometer Pin and Channel Definitions

// For Digital Thermometer (DS18B20)
#define DS18B20_PORT    PORTD
#define DS18B20_DDR     DDRD
#define DS18B20_PIN     PB2  // Example pin; adjust as per your hardware
#define DS18B20_INPUT    PIND

// For Analog Thermometer (LM35)
#define LM35_ADC_CHANNEL 0   // ADC0 (PORTA0)

// Select the Thermometer Type by Uncommenting One of the Following Lines
#define DIGITAL_THERMOMETER    // Uncomment if using DS18B20
//#define ANALOG_THERMOMETER    // Uncomment if using LM35

//-----------------------------------------------------------------------------
// Effect Functions
void running_fire_l(void);
void running_shadow(void);
void johnson_counter(void);
void blink_all(void);          
void binary_count(void);       
void handle_button(uint8_t kt);
void execute_effect(void);
void help(void);

// USART Functions
void USART_Init(uint32_t baud);
void USART_PutChar(uint8_t data);
uint8_t USART_Receive(void);
void print_string(const char *str);
void uart_receive_line_nb(void);

// Thermometer Functions
// Digital Thermometer (DS18B20)
// bool DS18B20_Init(void);
// void DS18B20_WriteByte(uint8_t byte);
// uint8_t DS18B20_ReadByte(void);
// bool initialize_thermometer(void);

// Analog Thermometer (LM35)
void ADC_Init(void);
uint16_t ADC_Read(void);
uint16_t read_temperature(uint8_t *dec);

// Temperature Sending Function
void send_temperature(void);

//-----------------------------------------------------------------------------
void send_to_hc595(uint8_t val)
{
    for (uint8_t i = 0; i < 8; i++) 
    {
        if (val & 0x80) {
            HC595_PORT |= (1 << HC595_DATA);
        } else {
            HC595_PORT &= ~(1 << HC595_DATA);
        }
        HC595_PORT |= (1 << HC595_SHCP); 
        val <<= 1;     
        _delay_us(5);                    
        HC595_PORT &= ~(1 << HC595_SHCP);     
    }
    HC595_PORT |= (1 << HC595_STCP);          
    _delay_us(10);                       
    HC595_PORT &= ~(1 << HC595_STCP);         
}

//-----------------------------------------------------------------------------
void USART_Init(uint32_t baud)
{
    // Calculate baud rate (U2X = 0)
    UBRR0H = (uint8_t)((F_CPU/(16*baud)-1)>>8);
    UBRR0L = (uint8_t)(F_CPU/(16*baud) - 1);
    UCSR0A = 0;
    // Enable transmitter and receiver
    UCSR0B = (1 << RXEN0)|(1<<TXEN0);
    // Set frame format: 8 data bits, 1 stop bit, no parity
    UCSR0C = (1<<UCSZ01)|(1<<UCSZ00);
}

//-----------------------------------------------------------------------------
void USART_PutChar(uint8_t data)
{
    // Wait for empty transmit buffer
    while (!(UCSR0A & (1<<UDRE0))){ ; }
    // Put data into buffer, sends the data
    UDR0 = data;
}

//-----------------------------------------------------------------------------
uint8_t USART_Receive(void)
{
    // Wait for data to be received
    while (!(UCSR0A & (1<<RXC0))){ ; }
    // Get and return received data from buffer
    return UDR0;
}

//-----------------------------------------------------------------------------
void print_string(const char *str)
{
    while (*str) {
        USART_PutChar(*str++);
    }
}

//-----------------------------------------------------------------------------
void uart_receive_line_nb(void)
{
    char tmp;

    if (UCSR0A & (1<<RXC0)) {
        tmp = UDR0;

        if ((tmp == '\n') && (rcv_index > 0)) {
            USART_PutChar(tmp);
            rcv_complete_handler(rcv_index);
            rcv_index = 0;
        } else {
            if ((rcv_index < 81) && (tmp != '\r')) {
                rcv_buff[rcv_index++] = tmp;
                rcv_buff[rcv_index] = '\0';
                USART_PutChar(tmp);
            }
        }
    }  
}

//-----------------------------------------------------------------------------
void help(void)
{
    print_string("Available commands:\r\n");
    print_string("start         - Start the current effect\r\n");
    print_string("stop          - Stop the current effect\r\n");
    print_string("speed <n>     - Set the effect speed (n: delay in ms)\r\n");
    print_string("effect <n>    - Select an effect (0: running_fire_l, 1: running_shadow, 2: johnson_counter, 3: blink_all, 4: binary_count)\r\n"); // UPDATED
    print_string("Tstart        - Start sending temperature every 2 seconds\r\n");
    print_string("Tstop         - Stop sending temperature\r\n");
    print_string("Ttemp         - Send temperature once\r\n");
    print_string("help          - Show this help message\r\n");
}

//-----------------------------------------------------------------------------
// Command Handler
void rcv_complete_handler(uint8_t len)
{
    if (strcmp((const char*)rcv_buff, "start") == 0) {
        effect_running = 1;
        print_string("Effect started\r\n");
    } 
    else if (strcmp((const char*)rcv_buff, "stop") == 0) {
        effect_running = 0;
        print_string("Effect stopped\r\n");
    } 
    else if (strncmp((const char*)rcv_buff, "speed", 5) == 0) {
        uint16_t new_speed = atoi(&rcv_buff[6]);
        if (new_speed > 0) {
            effect_dly = new_speed;
            print_string("Speed updated\r\n");
        } else {
            print_string("Invalid speed\r\n");
        }
    } 
    else if (strncmp((const char*)rcv_buff, "effect", 6) == 0) {
        uint8_t new_effect = atoi(&rcv_buff[7]);
        if (new_effect < 5) { // UPDATED: Allowing up to effect 4
            effect = new_effect;
            char msg[30];
            sprintf(msg, "Effect changed to %d\r\n", effect);
            print_string(msg);
        } else {
            print_string("Invalid effect\r\n");
        }
    }
    // Temperature Commands
    else if (strcmp((const char*)rcv_buff, "Tstart") == 0) {
        temp_sending_active = true;
        print_string("Temperature sending started\r\n");
    }
    else if (strcmp((const char*)rcv_buff, "Tstop") == 0) {
        temp_sending_active = false;
        print_string("Temperature sending stopped\r\n");
    }
    else if (strcmp((const char*)rcv_buff, "Ttemp") == 0) {
        send_temperature();
    }
    else if (strcmp((const char*)rcv_buff, "help") == 0) {
        help(); // Call help function
    }
    else {
        print_string("Invalid command\r\n");
    }
}

//-----------------------------------------------------------------------------
// Button Handler (Not integrated in main loop)
void handle_button(uint8_t kt) 
{
    if (kt == 2) {
        if (!effect_running) {
            effect = (effect + 1) % 5; // UPDATED: Modulo with 5 to include new effects
        }
        effect_running = 1;
    } else {
        effect_running = !effect_running;
    }
}

//-----------------------------------------------------------------------------
void running_fire_l(void) 
{
    static uint8_t d = 0x01;
    d <<= 1;
    if (d == 0) d = 0x01;
    send_to_hc595(d);
}

//-----------------------------------------------------------------------------
void running_shadow(void)
{
    static uint8_t d = 0x01;
    d <<= 1;
    if (d == 0) d = 0x01;
    send_to_hc595(~d);
}

//-----------------------------------------------------------------------------
void johnson_counter(void) 
{
    static uint8_t johnson = 0;
    if (johnson & 128) {
        johnson <<= 1; 
    } else {
        johnson <<= 1;
        johnson |= 1;
    }
    send_to_hc595(johnson);
}

//-----------------------------------------------------------------------------
void blink_all(void) 
{
    static bool led_on = false;
    if (led_on) {
        send_to_hc595(0xFF); // All LEDs on
    } else {
        send_to_hc595(0x00); // All LEDs off
    }
    led_on = !led_on;
}

//-----------------------------------------------------------------------------
void binary_count(void) 
{
    static uint8_t count = 0x00;
    send_to_hc595(count);
    count++;
}

//-----------------------------------------------------------------------------
// Execute Selected Effect
void execute_effect(void)
{
    switch (effect) {
        case 0: running_fire_l(); break;
        case 1: running_shadow(); break;
        case 2: johnson_counter(); break;
        case 3: blink_all(); break;        
        case 4: binary_count(); break;     
        default: send_to_hc595(0x00); break; // Safety: Turn off all LEDs for undefined effects
    }
}

//-----------------------------------------------------------------------------
void ADC_Init(void) 
{
    ADMUX = (1 << REFS1) | (1 << REFS0) | (1 << MUX0);
    // Включаємо АЦП та задаємо подільник частоти CLK/128
    ADCSRA= (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

//-----------------------------------------------------------------------------
uint16_t ADC_Read(void)
{
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    return ((uint32_t)ADCW * 1100) / 1024;
}

//-----------------------------------------------------------------------------
uint16_t read_temperature(uint8_t *dec) 
{ 
    uint16_t val = ADC_Read();
    *dec = val % 10; 
    return val / 10;
}

//-----------------------------------------------------------------------------
// Temperature Sending Function
void send_temperature(void) 
{
    uint8_t temp = 0;
        // For LM35
        uint8_t dec_c;
        temp = read_temperature(&dec_c);
        char temp_str[20];
        sprintf(temp_str, "Temp: %d.%d C\r\n", temp, dec_c);
        print_string(temp_str);
}
//==============================================================================
/* Main Function */
int main(void)
{
    uint16_t dly1 = 0;

    // Set PORTB as output for LEDs
    DDRB |= (1 << HC595_DATA) | (1 << HC595_SHCP) | (1 << HC595_STCP);
    PORTB &= ~((1 << HC595_SHCP) | (1 << HC595_STCP));
    send_to_hc595(0x81 + 0x18);
    //DDRB |= 1 << PB2;
    //DDRD = 0xFF;

    // Initialize USART at 9600 baud
    USART_Init(9600);
    print_string("Light Effects Machine Ready\r\nType 'help' to see available commands.\r\n");

    ADC_Init();
    print_string("ADC Initialized for LM35\r\n");
    for (;;) 
    {
        uart_receive_line_nb();

        if (effect_running && (++dly1 > effect_dly)) {
            dly1 = 0;
            execute_effect();
        }
        if (temp_sending_active) {
            if (++temp_delay_counter >= TEMP_DELAY_COUNT) {
                temp_delay_counter = 0;
                send_temperature();
            }
        }
        _delay_ms(2);
    }
    return 0;
}