#include "display/display.hpp"

#include "config/hardware_config.hpp"

#include "esp_log.h"
#include "esp_timer.h" // Добавлено для точного замера времени отрисовки (esp_timer_get_time)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace mixer::display {
namespace {

constexpr char kTag[] = "display";

}  // анонимное пространство имен

/**
 * @brief Инициализирует базовую заглушку для логирования.
 * Вызывается один раз при старте подсистемы дисплея, если реальное железо (HUB75) еще не подключено.
 */
esp_err_t LogDisplaySink::begin() {
    ESP_LOGI(kTag, "log display sink started; replace IDisplaySink for HUB75/LCD output");
    return ESP_OK;
}

/**
 * @brief Простой вывод фрейма в консоль для отладки.
 * Используется вместо матрицы, когда она недоступна или отключена в конфигурации.
 */
void LogDisplaySink::render(const DisplayFrame& frame) {
    if (!frame.valid) {
        ESP_LOGI(kTag, "weight: no valid sample yet");
        return;
    }

    ESP_LOGI(kTag, "%s: weight %.2f / target %.2f / remaining %.2f / shovels %.1f",
             frame.stage_name,
             frame.weight,
             frame.target_weight,
             frame.remaining_weight,
             frame.remaining_shovels);
}

/**
 * @brief Конструктор задачи обновления дисплея.
 * @param latest Ссылка на подсистему хранения последнего валидного веса (хранилище обработанных данных).
 * @param sink Ссылка на абстрактный интерфейс вывода (реализация HUB75, LCD или просто лог).
 */
DisplayTask::DisplayTask(processing::LatestWeightStore& latest, IDisplaySink& sink)
    : latest_(latest), sink_(sink) {}

/**
 * @brief Запускает задачу дисплея как отдельный поток FreeRTOS.
 * Вначале инициализирует железо (sink_.begin()), а затем создает задачу (taskEntry).
 */
esp_err_t DisplayTask::start() {
    // Инициализация целевого устройства вывода (например, матрицы HUB75)
    esp_err_t err = sink_.begin();
    if (err != ESP_OK) {
        return err;
    }

    // Создание отдельного потока для дисплея.
    // Вынесено в отдельный поток, чтобы тяжелые вычисления UI (отрисовка шлейфов, цветов)
    // не блокировали подсистему измерения веса (HX711) или Wi-Fi.
    const BaseType_t created = xTaskCreatePinnedToCore(
        &DisplayTask::taskEntry,
        "display_task",
        config::kDisplayTaskStackBytes, // Размер стека берется из конфига
        this,                           // Передаем указатель на себя, чтобы вызвать метод run()
        tskIDLE_PRIORITY + 1,           // Низкий приоритет, чтобы не мешать критичным задачам
        nullptr,
        tskNO_AFFINITY);

    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

/**
 * @brief Точка входа в задачу FreeRTOS. Просто кастует context и вызывает run().
 */
void DisplayTask::taskEntry(void* context) {
    static_cast<DisplayTask*>(context)->run();
}

/**
 * @brief Главный рабочий цикл обновления дисплея.
 * Крутится бесконечно. Отвечает за:
 * 1. Забор данных из подсистемы взвешивания.
 * 2. Формирование абстрактного пакета (фрейма) для отрисовки.
 * 3. Отправку фрейма в устройство вывода.
 * 4. Замеры времени рендеринга и сон (FPS control).
 */
void DisplayTask::run() {
    uint32_t diagnostic_tick = 0; // Счетчик тактов для анимаций UI (например, вращения спиннера)

    // Переменные для сбора статистики по времени отрисовки
    int64_t last_log_time = esp_timer_get_time();
    int64_t render_time_sum = 0;
    uint32_t render_count = 0;

    while (true) {
        // Читаем текущее состояние веса из хранилища (подсистема processing).
        // Это атомарная операция (Thread-safe). Мы получаем последний обработанный вес, 
        // не блокируя и не опрашивая датчики (HX711) напрямую, что соответствует правилу разделения.
        const domain::WeightState state = latest_.get();
        
        // Выбираем основной результат фильтра для вывода на экран.
        // Если в конфигурации больше 1 фильтра (например, сырой и скользящее среднее), 
        // берем второй (сглаженный), иначе первый. Это позволяет скрыть скачки датчика от пользователя.
        const domain::FilterOutput primary =
            state.filter_count > 1 ? state.filters[1] : state.filters[0];

        // Формируем структуру данных (фрейм) для передачи в подсистему рендеринга.
        // DisplayFrame - это абстрактный контракт. Само устройство отображения ничего не знает о рецептах или тензодатчиках.
        DisplayFrame frame{};
        
        // stage_name: Название текущей стадии замеса (например, "Цемент" или "Песок").
        // Сейчас берется из конфигурации, но в будущем должно предоставляться подсистемой управления рецептами.
        frame.stage_name = config::kDefaultBatchStageName;
        
        // weight: Текущий сглаженный вес из primary фильтра.
        frame.weight = primary.weight;
        
        // target_weight: Целевой вес, который необходимо достичь.
        frame.target_weight = config::kDefaultBatchTargetWeight;
        
        // remaining_weight: Сколько килограмм еще нужно досыпать в бетономешалку.
        frame.remaining_weight = frame.target_weight - frame.weight;
        
        // remaining_shovels: Сколько "лопат" примерно осталось закинуть рабочему.
        // Вычисляется делением оставшегося веса на заранее настроенный средний вес одной лопаты (kDefaultShovelWeight).
        frame.remaining_shovels =
            config::kDefaultShovelWeight > 0.0f
                ? frame.remaining_weight / config::kDefaultShovelWeight
                : 0.0f;
        frame.channel_weights = state.sample.channels;
        frame.channel_ready = state.sample.ready;
                
        // diagnostic_tick: Счетчик для анимаций (например, для класса Spinner).
        // Увеличивается на каждом кадре, независимо от состояния датчика.
        frame.diagnostic_tick = diagnostic_tick++;
        
        // valid: Флаг корректности данных. Если датчик отвалился или еще не откалиброван,
        // этот флаг будет false, и UI должен отрисовать состояние ошибки или ожидания.
        frame.valid = primary.valid;

        // Засекаем время начала рендеринга кадра (в микросекундах)
        int64_t start_time = esp_timer_get_time();
        
        // Отправляем сформированный фрейм на отрисовку.
        // На этапе выполнения здесь может быть либо Hub75DisplaySink, либо LogDisplaySink.
        sink_.render(frame);
        
        // Засекаем время окончания рендеринга кадра
        int64_t end_time = esp_timer_get_time();
        
        // Обновляем статистику
        render_time_sum += (end_time - start_time);
        render_count++;

        // Раз в 5 секунд выводим в лог среднее время отрисовки одного кадра
        if (end_time - last_log_time >= 5000000) {
            if (render_count > 0) {
                // Выводим усредненное значение времени на 1 кадр и сколько кадров успели отрисовать за 5 сек
                ESP_LOGI(kTag, "AVG Render: %lld us (over %lu frames)", render_time_sum / render_count, render_count);
            }
            // Сбрасываем счетчики для следующих 5 секунд
            last_log_time = end_time;
            render_time_sum = 0;
            render_count = 0;
        }

        // Засыпаем на 50 мс для обеспечения стабильных 20 FPS (Frames Per Second) обновления экрана.
        // 50мс - значение, заданное жестко (hardcode) для гарантии быстрого отклика UI, 
        // при этом не перегружающее процессор. Это освобождает процессорное время 
        // для задач с высоким приоритетом (опрос HX711, поддержание Wi-Fi соединения).
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

}  // пространство имен mixer::display
