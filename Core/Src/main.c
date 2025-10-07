

#include "main.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>


typedef struct {
    float x, y;        // float координаты
    float dx, dy;      // float направления
} Ball;

typedef struct {
	int16_t y;
	uint8_t height;
} Paddle;

typedef struct {
	uint8_t player;
	uint8_t ai;
} Score;

#define LCD_ADDR 0x27  // I2C address of LCD with PCF8574 backpack
#define LCD_COLS 16
#define LCD_ROWS 4

// Game constants
#define PADDLE_HEIGHT 1
#define BALL_CHAR '*'
#define PADDLE_CHAR '|'
#define EMPTY_CHAR ' '

#define ENCODER_SENSITIVITY 3
#define ENCODER_DELAY_MS 40
/*Для меньшей чувствительности:
Увеличьте ENCODER_SENSITIVITY до 4-6
Увеличьте ENCODER_DELAY_MS до 50-80
Для большей чувствительности:
Уменьшите ENCODER_SENSITIVITY до 2
Уменьшите ENCODER_DELAY_MS до 20*/

// Game timing
#define GAME_SPEED_MS 40
#define AI_REACTION_DELAY 0

I2C_HandleTypeDef hi2c1;
TIM_HandleTypeDef htim3;

/* USER CODE BEGIN PV */
// Game objects
Ball ball;
Paddle player_paddle;
Paddle ai_paddle;
Score score;
/* USER CODE BEGIN PV */
// Display buffers

char prev_buffer[LCD_ROWS][LCD_COLS + 1]; // предыдущий кадр
// Game state
uint8_t game_running = 1;
uint8_t ai_delay_counter = 0;
int16_t encoder_position = 0;
uint8_t button_pressed = 0;

// Display buffer
char display_buffer[LCD_ROWS][LCD_COLS + 1];

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM3_Init(void);

/* USER CODE BEGIN PFP */
// LCD Functions
void LCD_SendCommand(uint8_t cmd);
void LCD_SendData(uint8_t data);
void LCD_Init(void);
void LCD_Clear(void);
void LCD_SetCursor(uint8_t row, uint8_t col);
void LCD_PrintString(char* str);
void LCD_PrintChar(char ch);

// Game Functions

void Game_Init(void);

void Game_Update(void);
void Game_Render(void);
void Game_HandleInput(void);
void Game_UpdateAI(void);
void Game_CheckCollisions(void);
void Game_ResetBall(void);
void Game_ShowScore(void);

// Utility Functions
void Delay_Ms(uint32_t ms);
int16_t Get_Encoder_Position(void);
uint8_t Get_Button_State(void);

// LCD PCF8574 Interface Functions
void LCD_SendNibble(uint8_t nibble, uint8_t rs) {
	uint8_t data = 0;

	// D4–D7 подключены к P4–P7, поэтому сдвигаем nibble
	data = (nibble & 0xF0); // берем старшие 4 бита
	data |= (rs ? (1 << 0) : 0); // RS — P0
	data |= (1 << 3); // BL — подсветка (P3)
    
	// Подаем импульс EN (P2)
	data |= (1 << 2); // EN = 1
	HAL_I2C_Master_Transmit(&hi2c1, LCD_ADDR << 1, &data, 1, 100);

	data &= ~(1 << 2); // EN = 0
	HAL_I2C_Master_Transmit(&hi2c1, LCD_ADDR << 1, &data, 1, 100);
}

void LCD_SendCommand(uint8_t cmd) {
	LCD_SendNibble(cmd & 0xF0, 0);
	LCD_SendNibble((cmd << 4) & 0xF0, 0);
	if (cmd <= 3) HAL_Delay(5);
}

void LCD_SendData(uint8_t data) {
	LCD_SendNibble(data & 0xF0, 1);
	LCD_SendNibble((data << 4) & 0xF0, 1);
}

void LCD_Init(void) {
	HAL_Delay(5);
	LCD_SendNibble(0x30, 0);
	HAL_Delay(5);
	LCD_SendNibble(0x30, 0);
	HAL_Delay(1);
	LCD_SendNibble(0x30, 0);
	HAL_Delay(1);
	LCD_SendNibble(0x20, 0);
	HAL_Delay(1);
    
	LCD_SendCommand(0x28); // 4-bit mode, 2 lines, 5x8 font
	LCD_SendCommand(0x0C); // Display on, cursor off
	LCD_SendCommand(0x06); // Entry mode: increment cursor
	LCD_Clear();
}

void LCD_Clear(void) {
	LCD_SendCommand(0x01);
	HAL_Delay(2);
}

void LCD_SetCursor(uint8_t row, uint8_t col) {
	static const uint8_t row_offsets[] = { 0x00, 0x40, 0x10, 0x50 };
	if (row >= LCD_ROWS) row = 0; // защита от выхода за пределы
	LCD_SendCommand(0x80 | (row_offsets[row] + col));
}

void LCD_PrintString(char* str) {
	while (*str) {
		LCD_SendData(*str++);
	}
}

void LCD_PrintChar(char ch) {
	LCD_SendData(ch);
}

// Game Implementation
void Game_Init(void) {
    LCD_Clear();
    // Initialize ball in center with float coordinates
    ball.x = LCD_COLS / 2.0f;
    ball.y = LCD_ROWS / 2.0f;
    
    // Случайный угол ИСКЛЮЧАЯ 90° (±10°)
    float angle;
    do {
        angle = (30 + rand() % 120) * 3.14159f / 180.0f; // 30-150 градусов
    } while (fabsf(angle - 1.5708f) < 0.1745f); // исключаем 90° ±10° (80-100°)
    
    float speed = 0.3f;
    ball.dx = cosf(angle) * speed;
    ball.dy = sinf(angle) * speed;
    
    // Initialize paddles - теперь высота 1
    player_paddle.y = LCD_ROWS / 2;  // убрали - PADDLE_HEIGHT / 2
    player_paddle.height = PADDLE_HEIGHT;  // = 1
    ai_paddle.y = LCD_ROWS / 2;      // убрали - PADDLE_HEIGHT / 2  
    ai_paddle.height = PADDLE_HEIGHT;      // = 1
    
    // Initialize score
    score.player = 0;
    score.ai = 0;
    
    // Clear display buffer
    for (int i = 0; i < LCD_ROWS; i++) {
        for (int j = 0; j < LCD_COLS; j++) {
            display_buffer[i][j] = EMPTY_CHAR;
        }
        display_buffer[i][LCD_COLS] = '\0';
    }
}

void Game_HandleInput(void) {
    static int16_t last_encoder = 0;
    static uint8_t accumulator = 0;
    static uint32_t last_move_time = 0;
    
    int16_t current_encoder = Get_Encoder_Position();
    int16_t diff = current_encoder - last_encoder;

    if (diff != 0) {
        accumulator += abs(diff);
        
        // Комбинируем делитель шагов и временную задержку
        if (accumulator >= ENCODER_SENSITIVITY && (HAL_GetTick() - last_move_time > ENCODER_DELAY_MS)) {
            if (diff > 0 && player_paddle.y > 0) {
                player_paddle.y--;
            } 
            else if (diff < 0 && player_paddle.y < LCD_ROWS - PADDLE_HEIGHT) {
                player_paddle.y++;
            }
            accumulator = 0;
            last_move_time = HAL_GetTick();
        }
        
        last_encoder = current_encoder;
    }
    
    // Обработка кнопки (пауза/рестарт)
    static uint8_t button_last = 0;
    uint8_t button_now = Get_Button_State();

    if (button_now && !button_last) {
        // Нажатие кнопки
        if (!game_running) {
			LCD_Clear();
            Game_Init();
            game_running = 1;
        } else {
            // Добавьте паузу при нажатии во время игры
            // game_running = 0; // Раскомментируйте для паузы
        }
    }

    button_last = button_now;
}


void Game_UpdateAI(void) {
    if (++ai_delay_counter < AI_REACTION_DELAY) return;
    ai_delay_counter = 0;

    // целимся так, чтобы ракетка была на уровне мяча (без деления на 2)
    int target = ball.y;

    // зажимаем в пределах экрана
    if (target < 0) target = 0;
    if (target > LCD_ROWS - PADDLE_HEIGHT) target = LCD_ROWS - PADDLE_HEIGHT;

    if (ai_paddle.y < target)      ai_paddle.y++;
    else if (ai_paddle.y > target) ai_paddle.y--;
}


void Game_CheckCollisions(void) {
    // Top and bottom walls
    if (ball.y <= 0) {
        ball.y = 0;
        ball.dy = fabsf(ball.dy);  // отражаем вниз
    } else if (ball.y >= LCD_ROWS - 1) {
        ball.y = LCD_ROWS - 1;
        ball.dy = -fabsf(ball.dy); // отражаем вверх
    }
    
    // Player paddle collision
    if (ball.dx < 0 && ball.x <= 1.0f) {
        if (ball.x <= 0.5f) {
            if (ball.y >= player_paddle.y && ball.y < player_paddle.y + PADDLE_HEIGHT) {
                ball.x = 1.0f;
                ball.dx = fabsf(ball.dx);  // вправо
                
                // СЛУЧАЙНЫЙ ВЫБОР УГЛА: 30°, 45°, 60°, 145°, 150°, 160°
                float angles[] = {
                    30.0f * 3.14159f / 180.0f,   // 30° вверх-вправо
                    45.0f * 3.14159f / 180.0f,   // 45° вверх-вправо  
     
                    150.0f * 3.14159f / 180.0f,  // 150° вниз-вправо
                    160.0f * 3.14159f / 180.0f   // 160° вниз-вправо
                };
                float angle = angles[rand() % 6];
                
                float speed = 0.3f;
                ball.dx = cosf(angle) * speed;
                ball.dy = sinf(angle) * speed;
                
            } else {
                score.ai++;
                Game_ResetBall();
                return;
            }
        } else if (ball.y >= player_paddle.y && ball.y < player_paddle.y + PADDLE_HEIGHT) {
            // Прямое попадание в ракетку
            ball.x = 1.0f;
            ball.dx = fabsf(ball.dx);  // вправо
            
            // СЛУЧАЙНЫЙ ВЫБОР УГЛА
            float angles[] = {
                30.0f * 3.14159f / 180.0f,
                45.0f * 3.14159f / 180.0f, 
            
                150.0f * 3.14159f / 180.0f,
                160.0f * 3.14159f / 180.0f
            };
            float angle = angles[rand() % 6];
            
            float speed = 0.3f;
            ball.dx = cosf(angle) * speed;
            ball.dy = sinf(angle) * speed;
        }
    }
    
    // AI paddle collision
    if (ball.dx > 0 && ball.x >= LCD_COLS - 2.0f) {
        if (ball.x >= LCD_COLS - 0.5f) {
            if (ball.y >= ai_paddle.y && ball.y < ai_paddle.y + PADDLE_HEIGHT) {
                ball.x = LCD_COLS - 2.0f;
                ball.dx = -fabsf(ball.dx); // влево
                
                // СЛУЧАЙНЫЙ ВЫБОР УГЛА (те же углы, но для движения влево)
                float angles[] = {
                    30.0f * 3.14159f / 180.0f,   // 30° вверх-влево
                    45.0f * 3.14159f / 180.0f,   // 45° вверх-влево
                  
                    150.0f * 3.14159f / 180.0f,  // 150° вниз-влево
                    160.0f * 3.14159f / 180.0f   // 160° вниз-влево
                };
                float angle = angles[rand() % 6];
                
                float speed = 0.3f;
                ball.dx = -cosf(angle) * speed; // отрицательное для движения влево
                ball.dy = sinf(angle) * speed;
                
            } else {
                score.player++;
                Game_ResetBall();
                return;
            }
        } else if (ball.y >= ai_paddle.y && ball.y < ai_paddle.y + PADDLE_HEIGHT) {
            // Прямое попадание в ракетку
            ball.x = LCD_COLS - 2.0f;
            ball.dx = -fabsf(ball.dx); // влево
            
            // СЛУЧАЙНЫЙ ВЫБОР УГЛА
            float angles[] = {
                30.0f * 3.14159f / 180.0f,
                45.0f * 3.14159f / 180.0f,
              
                150.0f * 3.14159f / 180.0f,
                160.0f * 3.14159f / 180.0f
            };
            float angle = angles[rand() % 6];
            
            float speed = 0.3f;
            ball.dx = -cosf(angle) * speed;
            ball.dy = sinf(angle) * speed;
        }
    }
    
    // Score goals
    if (ball.x < 0) {
        score.ai++;
        Game_ResetBall();
    } else if (ball.x >= LCD_COLS) {
        score.player++;
        Game_ResetBall();
    }
    
    // Game over condition
    if (score.player >= 9 || score.ai >= 9) {
        game_running = 0;
    }
}







void Game_ResetBall(void) {
    ball.x = LCD_COLS / 2.0f;
    ball.y = LCD_ROWS / 2.0f;
    
    // Случайный угол при сбросе
    float angle = (30 + rand() % 60) * 3.14159f / 180.0f; // 30-90 градусов
    if (rand() % 2) angle = -angle; // случайное направление
    
    float speed = 0.3f;
    ball.dx = cosf(angle) * speed * (rand() % 2 ? 1.0f : -1.0f);
    ball.dy = sinf(angle) * speed;
    
    HAL_Delay(50);
}

void Game_Update(void) {
    if (!game_running) return;
    
    Game_HandleInput();
    Game_UpdateAI();
    
    // Move ball with float coordinates
    ball.x += ball.dx;
    ball.y += ball.dy;
    
    Game_CheckCollisions();
}

void Game_Render(void) {
    for (int i = 0; i < LCD_ROWS; i++) {
        for (int j = 0; j < LCD_COLS; j++) {
            display_buffer[i][j] = EMPTY_CHAR;
        }
    }

    // Рисуем игрока и AI (без изменений)
    for (int i = 0; i < player_paddle.height; i++) {
        int y = player_paddle.y + i;
        if (y >= 0 && y < LCD_ROWS) display_buffer[y][0] = PADDLE_CHAR;
    }
    for (int i = 0; i < ai_paddle.height; i++) {
        int y = ai_paddle.y + i;
        if (y >= 0 && y < LCD_ROWS) display_buffer[y][LCD_COLS - 1] = PADDLE_CHAR;
    }

    // Рисуем мяч - ОКРУГЛЯЕМ float координаты
    int ball_x = (int)(ball.x + 0.5f); // округление
    int ball_y = (int)(ball.y + 0.5f);
    if (ball_x >= 0 && ball_x < LCD_COLS && ball_y >= 0 && ball_y < LCD_ROWS) {
        display_buffer[ball_y][ball_x] = BALL_CHAR;
    }

    // Остальное без изменений...
    char score_str[8];
    sprintf(score_str, "%d:%d", score.player, score.ai);
    int score_pos = (LCD_COLS - strlen(score_str)) / 2;
    for (int i = 0; i < strlen(score_str); i++) {
        display_buffer[0][score_pos + i] = score_str[i];
    }

    for (int row = 0; row < LCD_ROWS; row++) {
        for (int col = 0; col < LCD_COLS; col++) {
            if (display_buffer[row][col] != prev_buffer[row][col]) {
                LCD_SetCursor(row, col);
                LCD_PrintChar(display_buffer[row][col]);
                prev_buffer[row][col] = display_buffer[row][col];
            }
        }
    }
}


void Game_ShowScore(void) {
	LCD_Clear();
     static uint8_t first_call = 1;
	// Заголовок
	LCD_SetCursor(1, 4);
	LCD_PrintString("GAME OVER");
    
	// Результат
	char result[8];
	sprintf(result, "%d : %d", score.player, score.ai);
	LCD_SetCursor(2, 6);
	LCD_PrintString(result);

	// Инструкция с очисткой строки до конца
	LCD_SetCursor(3, 0);
	LCD_PrintString("Press to restart  "); // добавляем пробелы для очистки

	// Ждём отпускания кнопки, если она была нажата
	while (Get_Button_State()) {
		HAL_Delay(5);
	}

	// Ждём нажатия кнопки
	while (!Get_Button_State()) {
		HAL_Delay(5);
	}

	// Ждём отпускания кнопки перед рестартом
	while (Get_Button_State()) {
		HAL_Delay(5);
	}

	// Сброс игры
	Game_Init();
	game_running = 1;
}

// Utility Functions
int16_t Get_Encoder_Position(void) {
	return (int16_t)__HAL_TIM_GET_COUNTER(&htim3);
}

uint8_t Get_Button_State(void) {
	return !HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_2); // Active low
}

void Delay_Ms(uint32_t ms) {
	HAL_Delay(ms);
}



int main(void)
{
	HAL_Init();
	SystemClock_Config();

	MX_GPIO_Init();
	MX_I2C1_Init();
	MX_TIM3_Init();

	LCD_Init();
	HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);
 __HAL_TIM_SET_COUNTER(&htim3, 0);  // сбрасываем

	Game_Init();
	LCD_Clear();
	LCD_SetCursor(1, 6);
	LCD_PrintString("PONG");

	LCD_SetCursor(2, 0);
	LCD_PrintString("Press to start");
	LCD_SetCursor(2, 13);
	LCD_PrintString("   ");
  
	// Wait for button press
	while (!Get_Button_State()) {
		HAL_Delay(5);
	}
	while (Get_Button_State()) {
		HAL_Delay(5);
	}

	LCD_Clear();
	while (1)
	{
		if (game_running) {
			Game_Update();
			Game_Render();
			HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); // Blink LED
		}
		else {
			Game_ShowScore();
		}
		HAL_Delay(GAME_SPEED_MS);
	}
}

void SystemClock_Config(void)
{

	RCC_OscInitTypeDef RCC_OscInitStruct = { 0 };
	RCC_ClkInitTypeDef RCC_ClkInitStruct = { 0 };
	__HAL_RCC_TIM3_CLK_ENABLE();
	__HAL_RCC_PWR_CLK_ENABLE();
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;
	RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
	RCC_OscInitStruct.PLL.PLLM = 8;
	RCC_OscInitStruct.PLL.PLLN = 84;
	RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
	RCC_OscInitStruct.PLL.PLLQ = 4;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
	{
		Error_Handler();
	}

	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
	                            | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
	{
		Error_Handler();
	}
}

static void MX_I2C1_Init(void)
{
	hi2c1.Instance = I2C1;
	hi2c1.Init.ClockSpeed = 100000;
	hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
	hi2c1.Init.OwnAddress1 = 0;
	hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
	hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
	hi2c1.Init.OwnAddress2 = 0;
	hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
	hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
	if (HAL_I2C_Init(&hi2c1) != HAL_OK)
	{
		Error_Handler();
	}
}

static void MX_TIM3_Init(void)
{
	TIM_Encoder_InitTypeDef sEncoderConfig = { 0 };
	TIM_MasterConfigTypeDef sMasterConfig = { 0 };

	htim3.Instance = TIM3;
	htim3.Init.Prescaler = 0;
	htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
	htim3.Init.Period = 65535;
	htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
	sEncoderConfig.EncoderMode = TIM_ENCODERMODE_TI12;
	sEncoderConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
	sEncoderConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
	sEncoderConfig.IC1Prescaler = TIM_ICPSC_DIV1;
	sEncoderConfig.IC1Filter = 0;
	sEncoderConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
	sEncoderConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
	sEncoderConfig.IC2Prescaler = TIM_ICPSC_DIV1;
	sEncoderConfig.IC2Filter = 0;
	if (HAL_TIM_Encoder_Init(&htim3, &sEncoderConfig) != HAL_OK)
	{
		Error_Handler();
	}
	sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
	sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
	if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
	{
		Error_Handler();
	}
}
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* Debug LED: PC13 */
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* I2C GPIO Configuration: PB6 (SCL), PB7 (SDA) */
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Encoder Inputs: PB4 (TIM3_CH1), PB5 (TIM3_CH2) */
    GPIO_InitStruct.Pin = GPIO_PIN_4 | GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Encoder Key: PB2 (KEY) */
    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}


void Error_Handler(void)
{
	
	__disable_irq();
	while (1)
	{
	}
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif 