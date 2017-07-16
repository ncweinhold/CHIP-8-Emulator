#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef linux
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <SDL2/SDL.h>

#define MEMSIZE  4096
#define DISPLAY  2048
#define REGISTERS  16
#define STACKDEPTH 16
#define OFFSET  0x200
#define SCALING    10

unsigned char chip8_fontset[80] = {
  0xF0, 0x90, 0x90, 0x90, 0xF0,
  0x20, 0x60, 0x20, 0x20, 0x70,
  0xF0, 0x10, 0xF0, 0x80, 0xF0,
  0xF0, 0x10, 0xF0, 0x10, 0xF0,
  0x90, 0x90, 0xF0, 0x10, 0x10,
  0xF0, 0x80, 0xF0, 0x10, 0xF0,
  0xF0, 0x80, 0xF0, 0x90, 0xF0,
  0xF0, 0x10, 0x20, 0x40, 0x40,
  0xF0, 0x90, 0xF0, 0x90, 0xF0,
  0xF0, 0x90, 0xF0, 0x10, 0xF0,
  0xF0, 0x90, 0xF0, 0x90, 0x90,
  0xE0, 0x90, 0xE0, 0x90, 0xE0,
  0xF0, 0x80, 0x80, 0x80, 0xF0,
  0xE0, 0x90, 0x90, 0x90, 0xE0,
  0xF0, 0x80, 0xF0, 0x80, 0xF0,
  0xF0, 0x80, 0xF0, 0x80, 0x80
};

/* SDL_GetKeyboardState(NULL) returns a Uint8* referring to the state
   I can then check whether a key from the following mapping is pressed
   by something like:
   
   const Uint8* state = SDL_GetKeyboardState(NULL);
   for (int keyIdx = 0; keyIdx < 16; keyIdx++) {
       if (state[key_mapping[keyIdx]]) {
           a recognised key was pressed - do whatever I need to do with it
       }
   }
*/
static int key_mapping[16] = {
  SDL_SCANCODE_X,
  SDL_SCANCODE_1,
  SDL_SCANCODE_2,
  SDL_SCANCODE_3,
  SDL_SCANCODE_Q,
  SDL_SCANCODE_W,
  SDL_SCANCODE_E,
  SDL_SCANCODE_A,
  SDL_SCANCODE_S,
  SDL_SCANCODE_D,
  SDL_SCANCODE_Z,
  SDL_SCANCODE_C,
  SDL_SCANCODE_4,
  SDL_SCANCODE_R,
  SDL_SCANCODE_F,
  SDL_SCANCODE_V
};

typedef struct chip8 {
  unsigned short opcode;
  unsigned short stackPointer;
  unsigned short indexRegister;
  unsigned short pc;
  unsigned char delayTimer;
  unsigned char soundTimer;
  unsigned char memory[MEMSIZE];
  unsigned char display[DISPLAY];
  unsigned char V[REGISTERS];
  unsigned short stack[STACKDEPTH];

  bool shouldRender;
} chip8;

void initialise_device(chip8* c);
void load_rom(chip8* c, char* filename);
void display_state(chip8* c);
void execute_clock_cycle(chip8* c);

void initialise_device(chip8* c) {
  /* initialise the random number generator with a seed - needed for the RND
     instruction */
  srand(time(NULL));
  memset(c->memory, 0, sizeof(c->memory));
  memset(c->V, 0, sizeof(c->V));
  memset(c->display, 0, sizeof(c->display));
  memset(c->stack, 0, sizeof(c->stack));

  c->pc = 0x200;
  c->opcode = 0;
  c->indexRegister = 0;
  c->stackPointer = 0;

  memcpy(c->memory + 0x50, chip8_fontset, sizeof(chip8_fontset));

  c->soundTimer = 0;
  c->delayTimer = 0;

  c->shouldRender = false;

  #ifdef DEBUG
  printf("Initialised device - outputting the initial state to stdout:\n");
  display_state(c);
  #endif
}

void display_state(chip8* c) {
  printf("Hex dump of memory:\n");
  for (int row = 0; row < 256; ++row) {
    printf("%02x  ", c->memory[(row * 16)] & 0xFF);
    printf("%02x  ", c->memory[(row * 16)+1] & 0xFF);
    printf("%02x  ", c->memory[(row * 16)+2] & 0xFF);
    printf("%02x  ", c->memory[(row * 16)+3] & 0xFF);
    printf("%02x  ", c->memory[(row * 16)+4] & 0xFF);
    printf("%02x  ", c->memory[(row * 16)+5] & 0xFF);
    printf("%02x  ", c->memory[(row * 16)+6] & 0xFF);
    printf("%02x  ", c->memory[(row * 16)+7] & 0xFF);
    printf("%02x  ", c->memory[(row * 16)+8] & 0xFF);
    printf("%02x  ", c->memory[(row * 16)+9] & 0xFF);
    printf("%02x  ", c->memory[(row * 16)+10] & 0xFF);
    printf("%02x  ", c->memory[(row * 16)+11] & 0xFF);
    printf("%02x  ", c->memory[(row * 16)+12] & 0xFF);
    printf("%02x  ", c->memory[(row * 16)+13] & 0xFF);
    printf("%02x  ", c->memory[(row * 16)+14] & 0xFF);
    printf("%02x  \n", c->memory[(row * 16)+15] & 0xFF);
  }
}

void update_timers(chip8* c) {
  if (c->delayTimer > 0) {
    c->delayTimer--;
  }
  if (c->soundTimer > 0) {
    c->soundTimer--;
  }
  if (c->soundTimer != 0) {
    printf("PLAYING A SOUND\n");
  }
}

void execute_clock_cycle(chip8* c) {
  unsigned char vx, vy, rndByte; /* some locals to help make some of the opcode decoding clearer */
  const unsigned char* state = NULL;
  c->opcode = c->memory[c->pc] << 8 | c->memory[c->pc+1];
  /*printf("Executing opcode: %x\n", c->opcode);*/

  switch (c->opcode & 0xF000) {
  case (0x0000):
    switch (c->opcode) {
    case (0x00E0):
      /* clear the display */
      memset(c->display, 0, sizeof(c->display));
      c->pc += 2;
      break;
    case (0x00EE):
      /* return from subroutine - set the program counter to the address
	 at the top of the stack, and then subtract 1 from the stack pointer */
      c->stackPointer -= 1;
      /* printf("The value on the stack is: %d\n", c->stack[c->stackPointer]);*/
      c->pc = c->stack[c->stackPointer];
      /*printf("The program counter is: %d\n", c->pc);
	printf("The opcode at this memory location is: %02x\n", (c->memory[c->pc] << 8 | c->memory[c->pc+1]));*/
      c->pc += 2;
      /*printf("The opcode at the following location is: %02x\n", (c->memory[c->pc] << 8 | c->memory[c->pc+1]));*/
      break;
    default:
      /* according to devernay.free.fr/hacks/chip8/C8TECH10.HTM the 0nnn 
	 instruction is only used on the old machines - modern interpreters
	 can ignore it. */
      printf("Checking I'm not here\n");
      break;
    }
    break;
  case (0x1000):
    /* 1nnn jumps to address - set the program counter to nnn */
    c->pc = (c->opcode & 0x0FFF);
    break;
  case (0x2000):
    /* 0x2nnn CALL addr - call the subroutine at nnn */
    c->stack[c->stackPointer] = c->pc;
    c->stackPointer += 1;
    c->pc = (c->opcode & 0x0FFF);
    break;
  case (0x3000):
    /* 3xkk SE Vx, byte - compare register V[x] with kk
       if equal, increment the program counter by two instructions (pc + 4)
    */
    if (c->V[(c->opcode & 0x0F00) >> 8] == (c->opcode & 0x00FF)) {
      c->pc += 4;
    } else {
      c->pc += 2;
    }
    break;
  case (0x4000):
    /* 0x4xkk skip next instruction if Vx != kk */
    if (c->V[(c->opcode & 0x0F00) >> 8] != (c->opcode & 0x00FF)) {
      c->pc += 4;
    } else {
      c->pc += 2;
    }
    break;
  case (0x5000):
    /* 0x5xy0 - skip next instruction if Vx = Vy */
    vx = (c->opcode & 0x0F00) >> 8;
    vy = (c->opcode & 0x00F0) >> 4;
    if (c->V[vx] == c->V[vy]) {
      c->pc += 4;
    } else {
      c->pc += 2;
    }
    break;
  case (0x6000):
    /* 6xkk set V[x] = kk */
    c->V[(c->opcode & 0xF00) >> 8] = (c->opcode & 0x00FF);
    c->pc += 2;
    break;
  case (0x7000):
    /* 7xkk - set V[x] = V[x] + kk */
    c->V[(c->opcode & 0x0F00) >> 8] += (c->opcode & 0x00FF);
    c->pc += 2;
    break;
  case (0x8000):
    switch (c->opcode & 0x000F) {
    case (0x0000):
      /* Set V[X] = V[Y] */
      vx = (c->opcode & 0x0F00) >> 8;
      vy = (c->opcode & 0x00F0) >> 4;
      c->V[vx] = c->V[vy];
      c->pc += 2;
      break;
    case (0x0001):
      /* set V[X] = V[X] | V[Y] */
      vx = (c->opcode & 0x0F00) >> 8;
      vy = (c->opcode & 0x00F0) >> 4;
      c->V[vx] = c->V[vx] | c->V[vy];
      c->pc += 2;
      break;
    case (0x0002):
      vx = (c->opcode & 0x0F00) >> 8;
      vy = (c->opcode & 0x00F0) >> 4;
      c->V[vx] = c->V[vx] & c->V[vy];
      c->pc += 2;
      break;
    case (0x0003):
      vx = (c->opcode & 0x0F00) >> 8;
      vy = (c->opcode & 0x00F0) >> 4;
      c->V[vx] = c->V[vx] ^ c->V[vy];
      c->pc += 2;
      break;
    case (0x0004):
      /* Set V[X] = V[X] + V[Y], set V[F] = carry */
      {
	vx = (c->opcode & 0x0F00) >> 8;
	vy = (c->opcode & 0x00F0) >> 4;
	int res = c->V[vx] + c->V[vy];
	if (res > 255) {
	  c->V[0xF] = 1;
	  res = res & 0xFF;
	} else {
	  c->V[0xF] = 0;
	}
	c->V[vx] = res;
      }
      c->pc += 2;
      break;
    case (0x0005):
      /* if V[x] > V[y] then set V[F] to 1 - else 0. Then perform
	 V[x] = V[x] - V[y] */
      vx = (c->opcode & 0x0F00) >> 8;
      vy = (c->opcode & 0x00F0) >> 4;
      if (c->V[vx] > c->V[vy]) {
	c->V[0xF] = 1;
      } else {
	c->V[0xF] = 0;
      }

      c->V[vx] -= c->V[vy];
      c->pc += 2;
	
      break;
    case (0x0006):
      /* if the least significant bit of V[x] is 1, then set V[F] to 1. Else set to 0. 
	 Then shift V[x] right by 1 place
       */
      vx = (c->opcode & 0x0F00) >> 8;
      c->V[0xF] = (c->V[vx] & 0x1);
      c->V[vx] = c->V[vx] >> 1;
      c->pc += 2;
      break;
    case (0x0007):
      /* if V[y] > V[x] then set V[F] to 1 else 0. Then set V[x] = V[y] - V[x] */
      vx = (c->opcode & 0x0F00) >> 8;
      vy = (c->opcode & 0x00F0) >> 4;
      if (c->V[vy] > c->V[vx]) {
	c->V[0xF] = 1;
      } else {
	c->V[0xF] = 0;
      }
      c->V[vx] = c->V[vy] - c->V[vx];
      c->pc += 2;
      break;
    case (0x000E):
      /* if the most significant bit of V[x] is 1, then set V[F] to 1. Else set V[F] to 0. Then shift
	 V[x] to the left by 1 */
      vx = (c->opcode & 0x0F00) >> 8;
      c->V[0xF] = ((c->V[vx] & 0x80) != 0);
      c->V[vx] = c->V[vx] << 1;
      c->pc += 2;
      break;
    }
    break;
  case (0x9000):
    /* skip next instruction if V[x] != V[y] */
    vx = (c->opcode & 0x0F00) >> 8;
    vy = (c->opcode & 0x00F0) >> 4;
    if (c->V[vx] != c->V[vy]) {
      c->pc += 4;
    } else {
      c->pc += 2;
    }
    break;
  case (0xA000):
    /* Annn - set I to nnn */
    c->indexRegister = c->opcode & 0x0FFF;
    c->pc += 2;
    break;
  case (0xB000):
    /* Bnnn - jumps to location nnn + v0. Program counter is set to nnn + v[0] */
    c->pc = ((c->opcode & 0x0FFF) + (c->V[0]));
    break;
  case (0xC000):
    rndByte = rand() % 256;
    c->V[(c->opcode & 0x0F00) >> 8] = rndByte & (c->opcode & 0x00FF);
    c->pc += 2;
    break;
  case (0xD000):
    {
      unsigned char x = c->V[(c->opcode & 0x0F00) >> 8];
      unsigned char y = c->V[(c->opcode & 0x00F0) >> 4];
      unsigned short height = c->opcode & 0x000F;
      unsigned short pixel;

      c->V[0xF] = 0;

      for (int yline = 0; yline < height; yline++) {
	pixel = c->memory[c->indexRegister + yline];
	for (int xline = 0; xline < 8; xline++) {
	  if (pixel & (0x80 >> xline)) {
	    if (c->display[(x + xline + ((y + yline) * 64))]) {
	      c->V[0xF] = 1;
	    }
	    c->display[x + xline + ((y + yline) * 64)] ^= 1;
	  }
	}
      }

      c->shouldRender = true;
      c->pc += 2;
    }
    break;
  case (0xE000):
    /* two cases Ex9E and ExA1 */
    switch (c->opcode & 0x00FF) {
    case (0x009E):
      /* skip next instruction if key with the value of Vx is pressed */
      state = SDL_GetKeyboardState(NULL);
      vx = (c->opcode & 0x0F00) >> 8;
      if (state[key_mapping[c->V[vx]]]) {
	c->pc += 4;
      } else {
	c->pc += 2;
      }
      
      break;
    case (0x00A1):
      /* skip next instruction if the key with the value of Vx is not pressed */
      state = SDL_GetKeyboardState(NULL);
      vx = (c->opcode & 0x0F00) >> 8;
      if (!state[key_mapping[c->V[vx]]]) {
	c->pc += 4;
      } else {
	c->pc += 2;
      }
      break;
    default:
      fprintf(stderr, "Unrecognised opcode\n");
      exit(EXIT_FAILURE);
    }
    break;
  case (0xF000):
    switch (c->opcode & 0x00FF) {
    case (0x0007):
      /* Fx07 - set Vx to be the value of the delay timer */
      vx = (c->opcode & 0x0F00) >> 8;
      c->V[vx] = c->delayTimer;
      c->pc += 2;
      break;
    case(0x000A):
      /* instruction Fx0A waits for a key press and then stores that value in
	 V[x]. From the Cowgod Chip8 reference, it states:
	 
	 All execution stops until a key is pressed....
	 
	 What I can do is 
	 get the SDL_KeyboardState(NULL)
	 then iterate over the 16 entries of the key mapping
	 for each entry
	 if state(key mapping) {
	   set value of V register
	   increment the program counter as required
	 }

	 then exit - since we update the program counter only when a key
	 is pressed - effectively, this instruction will be a NOOP until then
      */

      state = SDL_GetKeyboardState(NULL);
      for (int i = 0; i < 16; i++) {
	if (state[key_mapping[i]]) {
	  vx = (c->opcode & 0x0F00) >> 8;
	  c->V[vx] = key_mapping[i];
	  c->pc += 2;
	}
      }
      /* do not increment program counter here - so we will repeat this
	 instruction until a key is pressed
      */
      break;
    case (0x0015):
      /* set delay timer to the value of V[X] */
      c->delayTimer = c->V[(c->opcode & 0x0F00) >> 8];
      c->pc += 2;
      break;
    case (0x0018):
      /* set sound timer to the value of V[X] */
      c->soundTimer = c->V[(c->opcode & 0x0F00) >> 8];
      c->pc += 2;
      break;
    case (0x001E):
      /* set I = I + V[X] */
      vx = (c->opcode & 0x0F00) >> 8;
      c->indexRegister += c->V[vx];
      c->pc += 2;
      break;
    case (0x0029):
      /* Fx29 - set I to be the location of sprite for digit V[x] 
	 The font data is loaded in memory at offset 0x50
       */
      vx = (c->opcode & 0x0F00) >> 8;
      c->indexRegister = 0x50 + (c->V[vx]*5); /* each sprite is 5 bytes */
      c->pc += 2;
      break;
    case (0x0033):
      /* store BCD representation of Vx in memory locations I, I+1, I+2

	 hundreds go into I, tens digit into I+1, ones digit in I+2*/
      vx = (c->opcode & 0x0F00) >> 8;
      c->memory[c->indexRegister] = c->V[vx] / 100;
      c->memory[c->indexRegister+1] = (c->V[vx] / 10) % 10;
      c->memory[c->indexRegister+2] = c->V[vx] % 10;
      c->pc += 2;
      break;
    case (0x0055):
      /* 0xFx55 stores registers v0 through to vx into memory starting at i*/

      vx = (c->opcode & 0x0F00) >> 8;
      for (unsigned char idx = 0; idx <= vx; idx++) {
	c->memory[c->indexRegister + idx] = c->V[idx];
      }
      c->pc += 2;
      break;
    case (0x0065):
      /* 0xFx65: read registers v0 through to vx from memory starting at i */
      vx = (c->opcode & 0x0F00) >> 8;
      for (unsigned char idx = 0; idx <= vx; idx++) {
	c->V[idx] = c->memory[c->indexRegister + idx];
      }
      c->pc += 2;
      break;
    }
    break;
  default:
    fprintf(stderr, "Error - unrecognised opcode. Exiting...\n");
    printf("The opcode which caused the error was: %04x\n", c->opcode);
    exit(EXIT_FAILURE);
  }
}

void load_rom(chip8* c, char* filename) {
  printf("In load_rom\n");

  FILE* rom = fopen(filename, "rb");
  if (rom != NULL) {
    /* check the size of the ROM to make sure it can fit in to the buffer */
    int size = 0;
    #ifdef linux
    int fd = fileno(rom);
    struct stat buf;
    fstat(fd, &buf);
    size = buf.st_size;
    if (size > (MEMSIZE-OFFSET)) {
      fprintf(stderr, "The ROM %s is an incorrect size. Maximum size is %d. Got: %d\n", filename,  (MEMSIZE-OFFSET), size);
      exit(EXIT_FAILURE);
    }
    #else
    /* use the fseek trick here - I don't have a windows machine to test the proper way
       of getting a file size */
    fseek(rom, 0, SEEK_END);
    size = ftell(rom);
    rewind(rom);
    #endif

    size_t result = fread(c->memory + OFFSET, 1, size, rom);
    if (result != size) {
      fprintf(stderr, "There was an error whilst reading the ROM\n");
      exit(EXIT_FAILURE);
    }
    
  } else {
    fprintf(stderr, "Unable to open ROM: %s\n", filename);
    exit(EXIT_FAILURE);
  }

  #ifdef DEBUG
  printf("Loaded the rom: %s, outputting the current state to stdout:\n", filename);
  display_state(c);
  #endif
}

void render(chip8* c, SDL_Renderer* renderer, SDL_Texture* displayTexture) {
  if (c->shouldRender) {
    SDL_RenderClear(renderer);

    void *pixData = NULL;
    int pitch;
    
    unsigned int textureData[64*32];
    for (int x = 0; x < DISPLAY; x++) {
      textureData[x] = c->display[x] ? 0xFFFF : 0;
    }
 
    SDL_LockTexture(displayTexture, NULL, &pixData, &pitch);
    memcpy( pixData, textureData, sizeof(textureData) );
    SDL_UnlockTexture(displayTexture);

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, displayTexture, NULL, NULL);
    SDL_RenderPresent(renderer);
    c->shouldRender = false;
  }

}

void die(char* msg) {
  fputs(msg, stderr);
  exit(EXIT_FAILURE);
}
  
int main(int argc, char** argv) {
  chip8 device;

  if (argc != 2) {
    die("USAGE: ./chip8emu <rom_file>\n");
  }

  initialise_device(&device);
  load_rom(&device, argv[1]);

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    die("Unable to initialise SDL\n");
  }

  SDL_Window* window = SDL_CreateWindow("Chip8 Emulator - written by Nick Weinhold",
					SDL_WINDOWPOS_UNDEFINED,
					SDL_WINDOWPOS_UNDEFINED,
					640, 320, SDL_WINDOW_SHOWN);
  if (window == NULL) {
    die("Could not create an SDL window\n");
  }
  
  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (renderer == NULL) {
    die("Could not create an SDL renderer\n");
  }
  SDL_RenderSetLogicalSize(renderer, 64, 32);
  SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0xFF, 0xFF);
  SDL_RenderPresent(renderer);

  /* have set the logical size of the display above - so can simply map 1-1 from display memory
     to the texture */
  SDL_Texture* displayTexture = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window),
						  SDL_TEXTUREACCESS_STREAMING, 64, 32);

  if (displayTexture == NULL) {
    die("Could not create a texture for rendering\n");
  }

  SDL_Event event;
  bool running = true;
  struct timespec start, end;
  unsigned long int nsPerInstruction = 1100000; /* 1 million and 16 million */
  unsigned long int nsPerTimerUpdate = 16000000;
  clock_gettime(CLOCK_REALTIME, &start);
  clock_gettime(CLOCK_REALTIME, &end);
  unsigned long int startTicks = start.tv_nsec;
  unsigned long int delta = 0;

  atexit(SDL_Quit);
  /* hacks for timers follow */

  struct timespec timerStart, timerEnd;
  clock_gettime(CLOCK_REALTIME, &timerStart);
  clock_gettime(CLOCK_REALTIME, &timerEnd);
  
  while (running) {
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT) {
	running = false;
      }
      if (event.type == SDL_KEYUP) {
	if (event.key.keysym.sym == SDLK_ESCAPE) {
	  running = false;
	}
      }
    }
    
    if (end.tv_nsec - start.tv_nsec >= nsPerInstruction) {
      clock_gettime(CLOCK_REALTIME, &start);
      execute_clock_cycle(&device);
      render(&device, renderer, displayTexture);
      /*SDL_Delay(1);*/
    }

    if (timerEnd.tv_nsec - timerStart.tv_nsec > nsPerTimerUpdate) {
      
      update_timers(&device);
      /*SDL_Delay(1);*/

      clock_gettime(CLOCK_REALTIME, &timerStart);
    }
      
    
    clock_gettime(CLOCK_REALTIME, &end);
    clock_gettime(CLOCK_REALTIME, &timerEnd);
    SDL_Delay(1);

  }

  SDL_DestroyTexture(displayTexture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  
  return 0;
}
