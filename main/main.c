/*
 * LED blink with FreeRTOS
 */
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "ssd1306.h"
#include "gfx.h"

#include "pico/stdlib.h"
#include <stdio.h>

#define SOUND_SPEED_US 0.0343

const int ECHO_PIN = 16;
const int TRIGGER_PIN = 17;

SemaphoreHandle_t xSemaphoreTrigger;
QueueHandle_t xQueueTime;
QueueHandle_t xQueueDistance;

void echo_callback(uint gpio, uint32_t events) {
    uint64_t time;

    if (events == GPIO_IRQ_EDGE_FALL || events == GPIO_IRQ_EDGE_RISE) {
        time = to_us_since_boot(get_absolute_time());
        xQueueSendFromISR(xQueueTime, &time, 0);
    }
}

void trigger_task(void *p) {
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);

    while (1) {
        gpio_put(TRIGGER_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_put(TRIGGER_PIN, 0);
        xSemaphoreGive(xSemaphoreTrigger);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void echo_task(void *p) {
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(
        ECHO_PIN, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &echo_callback);

    uint64_t rise_time = 0;
    uint64_t time;
    int count = 0;

    while (1) {
        if (xQueueReceive(xQueueTime, &time, pdMS_TO_TICKS(100))) {
            if (count % 2 == 0) {
                rise_time = time;
            } else {
                float distance = (time - rise_time) * SOUND_SPEED_US / 2;
                printf("%f\n", distance);
                xQueueSend(xQueueDistance, &distance, pdMS_TO_TICKS(10));
            }
            count++;
        };
    }
}

void oled_task(void *p) {
    printf("Inicializando Driver\n");
    ssd1306_init();

    printf("Inicializando GLX\n");
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);

    char relative = 0;
    float distance;

    while (1) {
        char info[20];
        if ((xSemaphoreTake(xSemaphoreTrigger, pdMS_TO_TICKS(1000)) == pdTRUE) && ((xQueueReceive(xQueueDistance, &distance, pdMS_TO_TICKS(1000))))) {
            if (distance <= 300) {
                relative = distance * 128 / 300;
                sprintf(info, "%.2f cm", distance);
            } else {
                sprintf(info, "Fora de alcance");
            }
        } else {
            sprintf(info, "Fio Desconectado");
        }
        gfx_clear_buffer(&disp);
        gfx_draw_string(&disp, 0, 0, 1, info);
        gfx_draw_line(&disp, 15, 27, relative - 15, 27);
        gfx_show(&disp);
    }
}

int main() {
    stdio_init_all();

    xSemaphoreTrigger = xSemaphoreCreateBinary();

    if (xSemaphoreTrigger == NULL) {
        printf("Falha em criar o semaforo\n");
    }

    xQueueTime = xQueueCreate(32, sizeof(uint64_t));

    if (xQueueTime == NULL) {
        printf("Falha em criar a fila\n");
    }

    xQueueDistance = xQueueCreate(32, sizeof(float));

    if (xQueueDistance == NULL) {
        printf("Falha em criar a fila\n");
    }

    xTaskCreate(trigger_task, "Trigger", 4095, NULL, 1, NULL);
    xTaskCreate(echo_task, "Echo", 4095, NULL, 1, NULL);
    // xTaskCreate(oled_task, "Oled", 4095, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true)
        ;
}
