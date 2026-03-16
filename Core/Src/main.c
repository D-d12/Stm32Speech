#include "main.h"
#include "string.h"
#include "stdio.h"

I2S_HandleTypeDef  hi2s2;
DMA_HandleTypeDef  hdma_spi2_rx;
UART_HandleTypeDef huart2;

#define I2S_BUFFER_SIZE     2048
#define PCM_SIZE            (I2S_BUFFER_SIZE / 4)
#define SAMPLE_RATE         16000
#define RECORD_SECONDS      3
#define RECORD_BUFFER_SIZE  (SAMPLE_RATE * RECORD_SECONDS)

uint16_t i2s_rx_buf[I2S_BUFFER_SIZE];
int16_t  pcm_output[PCM_SIZE];
int16_t  record_buf[RECORD_BUFFER_SIZE];

volatile uint8_t data_ready   = 0;
volatile uint8_t is_recording = 0;
volatile uint8_t send_now     = 0;
uint32_t         record_index = 0;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2S2_Init(void);
void Process_Audio_Data(uint16_t *buffer_offset);

/* -------------------------------------------------------------------------- */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();

    // Force PA2/PA3 as USART2
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct2 = {0};
    GPIO_InitStruct2.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
    GPIO_InitStruct2.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct2.Pull      = GPIO_NOPULL;
    GPIO_InitStruct2.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct2.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct2);

    MX_USART2_UART_Init();
    MX_I2S2_Init();

    HAL_I2S_Receive_DMA(&hi2s2, i2s_rx_buf, I2S_BUFFER_SIZE);

    // Boot blink 3x
    for (int i = 0; i < 3; i++) {
        HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
        HAL_Delay(200);
        HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
        HAL_Delay(200);
    }

    // Send ready
    HAL_UART_Transmit(&huart2, (uint8_t*)"READY\n", 6, 100);

    while (1)
    {
        // Blue button starts recording
        if (HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_RESET
            && !is_recording && !send_now)
        {
            record_index = 0;
            is_recording = 1;
            HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
            HAL_Delay(50);
        }

        if (data_ready == 1)
        {
            data_ready = 0;

            if (is_recording)
            {
                for (int i = 0; i < PCM_SIZE && record_index < RECORD_BUFFER_SIZE; i++)
                {
                    record_buf[record_index++] = pcm_output[i];
                }

                if (record_index >= RECORD_BUFFER_SIZE)
                {
                    is_recording = 0;
                    send_now     = 1;
                    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
                }
            }
        }

        if (send_now)
        {
            send_now = 0;

            // Send header
            char header[64];
            uint32_t byte_count = record_index * 2;
            snprintf(header, sizeof(header), "PCM_START %lu\n", byte_count);
            HAL_UART_Transmit(&huart2, (uint8_t*)header, strlen(header), 500);
            HAL_Delay(100);

            // Send PCM bytes in chunks
            uint8_t  *raw  = (uint8_t*)record_buf;
            uint32_t total = byte_count;
            uint32_t sent  = 0;

            while (sent < total) {
                uint32_t chunk = ((total - sent) > 256) ? 256 : (total - sent);
                HAL_UART_Transmit(&huart2, &raw[sent], chunk, 1000);
                sent += chunk;
                HAL_Delay(5);
            }

            // Send end marker
            HAL_UART_Transmit(&huart2, (uint8_t*)"PCM_END\n", 8, 500);

            // Blink 5x
            for (int i = 0; i < 5; i++) {
                HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
                HAL_Delay(100);
                HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
                HAL_Delay(100);
            }

            // Ready for next
            HAL_UART_Transmit(&huart2, (uint8_t*)"READY\n", 6, 100);
        }
    }
}

/* -------------------------------------------------------------------------- */
void Process_Audio_Data(uint16_t *buffer_offset)
{
    int pcm_index = 0;
    for (int i = 0; i < (I2S_BUFFER_SIZE / 2); i += 2)
    {
        pcm_output[pcm_index++] = (int16_t)buffer_offset[i];
    }
}

void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    Process_Audio_Data(&i2s_rx_buf[0]);
    data_ready = 1;
}

void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    Process_Audio_Data(&i2s_rx_buf[I2S_BUFFER_SIZE / 2]);
    data_ready = 1;
}

/* -------------------------------------------------------------------------- */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef       RCC_OscInitStruct   = {0};
    RCC_ClkInitTypeDef       RCC_ClkInitStruct   = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM            = 16;
    RCC_OscInitStruct.PLL.PLLN            = 336;
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV4;
    RCC_OscInitStruct.PLL.PLLQ            = 2;
    RCC_OscInitStruct.PLL.PLLR            = 2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) Error_Handler();

    PeriphClkInitStruct.PeriphClockSelection  = RCC_PERIPHCLK_I2S_APB1;
    PeriphClkInitStruct.PLLI2S.PLLI2SN        = 192;
    PeriphClkInitStruct.PLLI2S.PLLI2SR        = 2;
    PeriphClkInitStruct.I2sApb1ClockSelection = RCC_I2SAPB1CLKSOURCE_PLLI2S;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) Error_Handler();
}

static void MX_I2S2_Init(void)
{
    hi2s2.Instance            = SPI2;
    hi2s2.Init.Mode           = I2S_MODE_MASTER_RX;
    hi2s2.Init.Standard       = I2S_STANDARD_PHILIPS;
    hi2s2.Init.DataFormat     = I2S_DATAFORMAT_16B_EXTENDED;
    hi2s2.Init.MCLKOutput     = I2S_MCLKOUTPUT_DISABLE;
    hi2s2.Init.AudioFreq      = I2S_AUDIOFREQ_16K;
    hi2s2.Init.CPOL           = I2S_CPOL_LOW;
    hi2s2.Init.ClockSource    = I2S_CLOCK_PLL;
    hi2s2.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
    if (HAL_I2S_Init(&hi2s2) != HAL_OK) Error_Handler();
}

static void MX_USART2_UART_Init(void)
{
    __HAL_RCC_USART2_CLK_ENABLE();
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler();
}

static void MX_DMA_Init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();
    HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin   = LD2_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin  = B1_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}
