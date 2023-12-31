#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "vga_graphics.h"

/*ROMS*/
#include "Space_Invaders.h"

typedef struct {
    uint16_t window_width;
    uint16_t window_height;
    uint8_t fg_color;
    uint8_t bg_color;
    uint8_t scale_factor;
    uint16_t insts_per_second; 
} config_t;

typedef enum {
    QUIT,
    RUNNING,
    PAUSED,
}emulator_state_t;

// CHIP8 Instruction format
typedef struct {
    uint16_t opcode;
    uint16_t NNN;   // 12 bit address/constant
    uint8_t NN;     // 8 bit constant
    uint8_t N;      // 4 bit constant
    uint8_t X;      // 4 bit register identifier
    uint8_t Y;      // 4 bit register identifier
} instruction_t;

typedef struct {
    emulator_state_t state;
    uint8_t ram[4096];
    bool display[64*32];
    uint16_t stack[12]; //subroutines stack
    uint16_t *stack_ptr;
    uint8_t V[16];      //Data registers V0-VF
    uint16_t I;         //Index register
    uint16_t PC;            // Program Counter
    uint8_t delay_timer;    // Decrements at 60hz when >0
    uint8_t sound_timer;    // Decrements at 60hz and plays tone when >0 
    bool keypad[16];        // Hexadecimal keypad 0x0-0xF
    const char *rom_name;   // Currently running ROM
    instruction_t inst;     // Currently executing instruction
    bool draw;              // Update the screen yes/no
}chip8_t;


void set_config_from_args(config_t *config){

    *config = (config_t){
        .window_width = 64,
        .window_height = 32,
        .fg_color = WHITE, 
        .bg_color = BLACK, /*color negro rbga*/
        .scale_factor = 10,
        .insts_per_second = 600 
    };

}


void update_screen(const config_t config, const chip8_t chip8) {

    draw_display(chip8.display);

}

void initKeypad(void){

    uint32_t pines_mask;
    uint32_t pines_mask_direction;

    //columnas
    for(int i = 7; i < 11; i++){

        pines_mask |= (1 << i); 
    }

    //filas
    for(int i = 26; i < 30; i++){

        pines_mask |= (1 << i); 
    }

    gpio_init_mask(pines_mask);

    for(int i = 26; i < 30; i++){
        pines_mask_direction |= (1 << i);           //mask para la direccion de los pines como salida (filas)
    }

    gpio_set_dir_masked(pines_mask, pines_mask_direction);          //seteando las direccion de los pines   

}




void handle_input(chip8_t *chip8){
    for(int i = 26; i < 30; i++){
            gpio_put(i, 1);
            for(int j = 7; j < 11; j++){
                uint8_t pos = (j - 7) + ((i-26) * 4);
                chip8->keypad[pos] = gpio_get(j);
            }
            gpio_put(i, 0);
    }
}

void init_chip8(chip8_t *chip8){

    const uint16_t entry_point = 0x200;

    const uint8_t font[] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0,   // 0   => 1111_0000 | 1001_0000 | 1001_0000 | 1001_0000 | 1111_0000
        0x20, 0x60, 0x20, 0x20, 0x70,   // 1  
        0xF0, 0x10, 0xF0, 0x80, 0xF0,   // 2 
        0xF0, 0x10, 0xF0, 0x10, 0xF0,   // 3
        0x90, 0x90, 0xF0, 0x10, 0x10,   // 4    
        0xF0, 0x80, 0xF0, 0x10, 0xF0,   // 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0,   // 6
        0xF0, 0x10, 0x20, 0x40, 0x40,   // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0,   // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0,   // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90,   // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0,   // B
        0xF0, 0x80, 0x80, 0x80, 0xF0,   // C
        0xE0, 0x90, 0x90, 0x90, 0xE0,   // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0,   // E
        0xF0, 0x80, 0xF0, 0x80, 0x80,   // F
    };

    // Load font 
    memcpy(&chip8->ram[0], font, sizeof(font));

    // Open ROM file
    /*YA CARGADA EN EL .h file*/

    // Get/check rom size

    const uint16_t max_size = sizeof(chip8->ram) - entry_point;

    if (ROM_SIZE > max_size) {
        printf("Rom file is too big! Rom size: %d, Max size allowed: %d\n", ROM_SIZE, max_size);
    }

    // Load ROM
    memcpy(&chip8->ram[entry_point], ROM_NAME, sizeof(ROM_NAME));
    
    chip8->state = RUNNING;
    chip8->PC = entry_point;
    chip8->rom_name = "Space_Invaders";   //ejemplo
    chip8->stack_ptr = chip8->stack;

}

/*
void final_cleanup(sdl_t *sdl){
    
    SDL_DestroyRenderer(sdl->renderer);
    SDL_DestroyWindow(sdl->window);
    SDL_Quit();
}
*/

void emulate_instruction(chip8_t *chip8, const config_t config){

    bool carry;   // Save carry flag/VF value for some instructions

    //toma dos bytes de la ram con posicion dada por PC(PROGRAM COUNTER)
    chip8->inst.opcode = (chip8->ram[chip8->PC] << 8) | chip8->ram[chip8->PC+1];
    chip8->PC += 2;         //Las siguientes dos instrucciones 

    // Fill out current instruction format (divisiones de 4 bits)
    chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
    chip8->inst.NN = chip8->inst.opcode & 0x0FF;
    chip8->inst.N = chip8->inst.opcode & 0x0F;
    chip8->inst.X = (chip8->inst.opcode >> 8) & 0x0F;
    chip8->inst.Y = (chip8->inst.opcode >> 4) & 0x0F;

    switch ((chip8->inst.opcode>>12) & 0x0F){
        case 0x0:
            if(chip8->inst.NNN == 0x0E0){
            //clear the screen
            memset(&chip8->display[0], false, sizeof(chip8->display));  
            }
            else if(chip8->inst.NNN == 0x0EE){
                // 0x00EE: Return from subroutine
                //Retorna de una subrutina. Se decrementa en 1 el Stack Pointer (SP). 
                //El intérprete establece el Program Counter como la dirección donde apunta el SP en la Pila.

                //chip8->PC = *chip8->stack_ptr--;      => AL PARECER NO ES LO MISMO

                chip8->PC = *--chip8->stack_ptr;
            }
            break;
        case 0x1:
            //Salta a la dirección NNN. El intérprete establece el Program Counter a NNN.
            chip8->PC = chip8->inst.NNN;
            break;
        case 0x2:
            // 0x2NNN: Call subroutine at NNN
            // Store current address to return to on subroutine stack ("push" it on the stack)
            //   and set program counter to subroutine address so that the next opcode
            //   is gotten from there.
            *chip8->stack_ptr++ = chip8->PC;  
            chip8->PC = chip8->inst.NNN;
            break;
        case 0x3:
            //Salta a la siguiente instrucción si VX = NN. 
            //El intérprete compara el registro VX con el NN, y si son iguales, incrementa el PC en 2. 
            
            if(chip8->V[chip8->inst.X] == chip8->inst.NN){
                chip8->PC += 2;       // Skip next opcode/instruction
            }
            break;
        case 0x4:
            //Formato => 4XKK 
            //  Salta a la siguiente instrucción si VX != KK.
            // El intérprete compara el registro VX con el KK, y si no son iguales, incrementa el PC en 2. 
            if(chip8->V[chip8->inst.X] != chip8->inst.NN){
                chip8->PC += 2;
            }
            break;
        case 0x5:
            //Formato => 5XY0 
            //Salta a la siguiente instrucción si VX = VY. 
            //El intérprete compara el registro VX con el VY, y si son iguales, incrementa el PC en 2.
            if(chip8->inst.N != 0){
                //WRONG OPCODE
                break;
            }

            if(chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y]){
                chip8->PC += 2;
            }
            break; 
        case 0x6:
            //formato => 6XKK
            //Hace VX = KK. El intérprete coloca el valor KK dentro del registro VX. 
            chip8->V[chip8->inst.X] = chip8->inst.NN;
            break;
        case 0x7:
            //formato => 7XKK
            //Hace VX = VX + KK. Suma el valor de KK al valor de VX y el resultado lo deja en VX.
            chip8->V[chip8->inst.X] += chip8->inst.NN;
            break;
        case 0x08:
            //formato => 8XY?
            switch (chip8->inst.N){
                case 0x0:
                    //Hace VX = VY. Almacena el valor del registro VY en el registro VX. 
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y];
                    break;
                case 0x1:
                    //Hace VX = VX OR VY.
                    chip8->V[chip8->inst.X] |= chip8->V[chip8->inst.Y];
                    break;
                case 0x2:
                    //Hace VX = VX AND VY. 
                    chip8->V[chip8->inst.X] &= chip8->V[chip8->inst.Y];
                    break;
                case 0x3:
                    //Hace VX = VX XOR VY.
                    chip8->V[chip8->inst.X] ^= chip8->V[chip8->inst.Y];
                    break;
                case 0x4:
                    //Suma VY a VX. VF se pone a 1 cuando hay un acarreo (carry), y a 0 cuando no.
                    //Practicamente detectar si hay desborde al hacer la suma

                    carry = ((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]) > 255);

                    chip8->V[chip8->inst.X] += chip8->V[chip8->inst.Y];

                    chip8->V[0xF] = carry;

                    break;
                case 0x5:
                    //VY se resta de VX. 
                    //VF se pone a 0 cuando hay que restarle un dígito al número de la izquierda
                    //más conocido como "pedir prestado" o borrow, y se pone a 1 cuando no es necesario
                    //UNDERFLOW

                    carry = (chip8->V[chip8->inst.Y] <= chip8->V[chip8->inst.X]);

                    chip8->V[chip8->inst.X] -= chip8->V[chip8->inst.Y];

                    chip8->V[0xF] = carry;

                    break;
                case 0x6:
                    //Establece VF = 1 o 0 según bit menos significativo de VX y se divide VX por 2.

                    carry = chip8->V[chip8->inst.X] & 0x1;

                    chip8->V[chip8->inst.X] >>= 1;

                    chip8->V[0xF] = carry;

                    break;
                case 0x7:
                    //Si VY > VX => VF = 1, sino 0. VX = VY - VX.

                    carry = (chip8->V[chip8->inst.X] <= chip8->V[chip8->inst.Y]);

                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];

                    chip8->V[0xF] = carry;

                    break;
                case 0xE:
                    //Establece VF = 1 o 0 según bit más significativo de VX. Multiplica VX por 2.

                    carry = (chip8->V[chip8->inst.X] >> 7);

                    chip8->V[chip8->inst.X] <<= 1;

                    chip8->V[0xF] = carry;

                    break;

                default:
                    break;
            }

            break;
        case 0x9:
            //formato => 9XY0
            //Salta a la siguiente instrucción si VX != VY.
            if(chip8->V[chip8->inst.X] != chip8->V[chip8->inst.Y]){
                chip8->PC += 2;
            }
            break; 
        case 0xA:
            //formato => ANNN
            //Establece I = NNN. 
            chip8->I = chip8->inst.NNN;
            break;
        case 0xB:
            //formato => BNNN
            //Salta a la ubicación V[0]+ NNN. 
            chip8->PC = chip8->V[0] + chip8->inst.NNN;
            break;
        case 0xC:
            //formato => CXKK
            //Establece VX = un Byte Aleatorio AND KK.
            chip8->V[chip8->inst.X] = (rand() % 256) & chip8->inst.NN;
            break;
        case 0xD:
            //formato => DXYN
            //Pinta un sprite en la pantalla.
            //El intérprete lee N bytes desde la memoria, comenzando desde el contenido del registro I. 
            //Y se muestra dicho byte en las posiciones VX, VY de la pantalla.
            uint8_t X_coord = chip8->V[chip8->inst.X] % config.window_width;
            uint8_t Y_coord = chip8->V[chip8->inst.Y] % config.window_height;
            const uint8_t orig_X = X_coord; // Original X value

            chip8->V[0xF] = 0;  // Initialize carry flag to 0

            // Loop over all N rows of the sprite
            for (uint8_t i = 0; i < chip8->inst.N; i++) {
                // Get next byte/row of sprite data
                const uint8_t sprite_data = chip8->ram[chip8->I + i];
                X_coord = orig_X;   // Reset X for next row to draw

                //con el mayor igual a cero si da 8 vueltas
                for (int8_t j = 7; j >= 0; j--) {
                    // If sprite pixel/bit is on and display pixel is on, set carry flag
                    bool *pixel = &chip8->display[Y_coord * config.window_width + X_coord]; 
                    const bool sprite_bit = (sprite_data & (1 << j));

                    if (sprite_bit && *pixel) {
                        chip8->V[0xF] = 1;  
                    }

                    // XOR display pixel with sprite pixel/bit to set it on or off
                    *pixel ^= sprite_bit;

                    // Stop drawing this row if hit right edge of screen
                    if (++X_coord >= config.window_width) break;
                }

                // Stop drawing entire sprite if hit bottom edge of screen
                if (++Y_coord >= config.window_height) break;
            }
            chip8->draw = true; // Will update screen on next 60hz tick
            break;
        case 0xE:
            //formato => EX9E 
            //Salta a la siguiente instrucción si valor de VX coincide con tecla presionada. 
            if(chip8->inst.NN == 0x9E){
                if(chip8->keypad[chip8->V[chip8->inst.X]]){
                    chip8->PC += 2;
                }
            }
            //formato => EXA1 
            //Salta a la siguiente instrucción si valor de VX no coincide con tecla presionada (soltar tecla).
            else if(chip8->inst.NN == 0xA1){
                if(!chip8->keypad[chip8->V[chip8->inst.X]]){
                    chip8->PC += 2;
                }
            }
            break;
        case 0xF:
            switch (chip8->inst.NN){
                case 0x07:
                    //formato => FX07
                    //Establece Vx = valor del delay timer. 
                    chip8->V[chip8->inst.X] = chip8->delay_timer;
                break;
                case 0x0A:
                    // formato => FX0A 
                    //Espera por una tecla presionada y la almacena en el registro VX.
                    // 0xFX0A: VX = get_key(); Await until a keypress, and store in VX
                    static bool any_key_pressed = false;
                    static uint8_t key = 0xFF;

                    for (uint8_t i = 0; key == 0xFF && i < 16; i++) 
                        if (chip8->keypad[i]) {
                            key = i;    // Save pressed key to check until it is released
                            any_key_pressed = true;
                            break;
                        }

                    // If no key has been pressed yet, keep getting the current opcode & running this instruction
                    if (!any_key_pressed) chip8->PC -= 2; 
                    else {
                        // A key has been pressed, also wait until it is released to set the key in VX
                        if (chip8->keypad[key])     // "Busy loop" CHIP8 emulation until key is released
                            chip8->PC -= 2;
                        else {
                            chip8->V[chip8->inst.X] = key;     // VX = key 
                            key = 0xFF;                        // Reset key to not found 
                            any_key_pressed = false;           // Reset to nothing pressed yet
                        }
                    }
                    break;
                case 0x15:
                    //formato => FX15
                    //Establece Delay Timer = VX.
                    chip8->delay_timer = chip8->V[chip8->inst.X];
                    break;
                case 0x18:
                    //formato => FX18 
                    //Establece Sound Timer = VX.
                    chip8->sound_timer = chip8->V[chip8->inst.X];
                    break;
                case 0x1E:
                    //formato => FX1E
                    //Indice = Índice + VX.
                    chip8->I += chip8->V[chip8->inst.X];
                    break;
                case 0x29:
                    //formato => FX29
                    //Establece I = VX * largo Sprite Chip-8.
                    chip8->I = chip8->V[chip8->inst.X] * 5;
                    break;
                case 0x33:
                    //formato => FX33 
                    //Guarda la representación del valor de VX en formato humano.
                    //Poniendo las centenas en la posición de memoria I
                    //las decenas en I + 1 y las unidades en I + 2

                    uint8_t bcd = chip8->V[chip8->inst.X]; 
                    chip8->ram[chip8->I+2] = bcd % 10;
                    bcd /= 10;
                    chip8->ram[chip8->I+1] = bcd % 10;
                    bcd /= 10;
                    chip8->ram[chip8->I] = bcd;
                    break;
                case 0x55:
                    //formato => FX55
                    //Almacena el contenido de V0 a VX en la memoria empezando por la dirección I (Incluyendo posicion X)
                    memcpy(&chip8->ram[chip8->I], &chip8->V[0], (chip8->inst.X + 1));
                    break;
                case 0x65:
                    //formato => FX65
                    //Almacena el contenido de la dirección de memoria I en los registros del V0 al VX (Incluyendo posicion X)
                    memcpy(&chip8->V[0], &chip8->ram[chip8->I], (chip8->inst.X + 1));
                    break; 
            }
            break;

        default:
            break;

    }
}


// Update CHIP8 delay and sound timers every 60hz
void update_timers(chip8_t *chip8) {
    if (chip8->delay_timer > 0){
        chip8->delay_timer--;
    }

    if(chip8->sound_timer > 0){
        chip8->sound_timer--;
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
    }
    else{
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
    }
}



int main()
{
    stdio_init_all();

    //Initialize LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    // Initialize the VGA screen
    initVGA() ;

    //Initialize the keypad
    initKeypad();

    //Incializacion de la configuracion
    config_t config;

    //configuracion inicial
    set_config_from_args(&config);

    //estructura del chip8
    chip8_t chip8 = {0};

    //incializacion de valores de la estructura
    init_chip8(&chip8);

    //limpiar la pantalla
    clear_screen();
    

    while(chip8.state != QUIT){

        uint64_t time = time_us_64();

        handle_input(&chip8);
        
        //ejecuta 10 instrucciones cada 16ms para un total de 600 instrucciones por segundo
        for(uint8_t i = 0; i < config.insts_per_second/60; i++){
            emulate_instruction(&chip8, config);
        }

        uint64_t time_elapsed = time_us_64() - time;


        sleep_us(1600 > time_elapsed ? 1600 - time_elapsed : 0);

        update_screen(config, chip8);

        update_timers(&chip8);

    }

    return 0;
}
