#if 1
#include "App_ChatGPT.h"
#include "../../../ChappieBsp/Chappie.h"
LV_IMG_DECLARE(ui_img_icon_ChatGPT_png);

static std::string app_name = "ChatGPT";
static CHAPPIE* device;
static bool isRecording = false;
static bool isPlaying = false;
static lv_obj_t* record_btn;
static lv_obj_t* play_btn;
static lv_obj_t* record_label;
static lv_obj_t* play_label;
static std::vector<int16_t> recorded_audio;
static int16_t* _rawData;
/*
These values can be changed in order to evaluate the functions
*/
const uint16_t  _samples = 256; //This value MUST ALWAYS be a power of 2
const double    _samplingFrequency = 48000;
namespace App {

    /**
     * @brief Return the App name launcher, which will be shown on launcher App list
     * 
     * @return std::string 
     */
    std::string App_ChatGPT_appName()
    {
        return app_name;
    }

    /**
     * @brief Return the App Icon launcher, NULL for default
     * 
     * @return void* 
     */
    void* App_ChatGPT_appIcon()
    {
        return (void*)&ui_img_icon_ChatGPT_png;
    }

    static void _GPT_init()
    {
        _rawData    = new int16_t[_samples];
        
    }

    /**
     * @brief Start recording audio
     * 
     */
    void startRecording()
    {
        if (!isRecording) {
            isRecording = true;
            recorded_audio.clear();
            device->Mic.record(_rawData, _samples);
            // Start recording audio from the microphone
            if (isRecording) {
                UI_LOG("Recording started\n");
            } else {
                isRecording = false;
                UI_LOG("Failed to start recording\n");
            }
        }
    }

    /**
     * @brief Stop recording audio
     * 
     */
    void stopRecording()
    {
        if (isRecording) {
            isRecording = false;
            // Stop recording audio
            // Start recording audio from the microphone
            if (device->Mic.isRecording()) {
                UI_LOG("Recording stoped\n");
            } else {
                isRecording = false;
                UI_LOG("Failed to stop recording\n");
            }

        }
    }

    /**
     * @brief Play the recorded audio
     * 
     */
    void playAudio()
    {
        if (!isPlaying) {
            isPlaying = true;
            device->Speaker.setVolume(200);
            device->Speaker.playRaw(_rawData, _samples);
        };
        for (size_t i = 0; i < _samples; ++i) {
            printf("Sample %zu: %d\n", i, _rawData[i]);
        }
    }

    /**
     * @brief Record button event handler
     * 
     * @param event 
     */
    void record_btn_event_handler(lv_event_t* event)
    {
        if (isRecording) {
            stopRecording();
            lv_label_set_text(record_label, "Record");
        } else {
            startRecording();
            lv_label_set_text(record_label, "Stop");
        }
    }

    /**
     * @brief Play button event handler
     * 
     * @param event 
     */
    void play_btn_event_handler(lv_event_t* event)
    {
        if (!isPlaying) {
            playAudio();
        }
    }

    /**
     * @brief Called when App is on create
     * 
     */
    void App_ChatGPT_onCreate()
    {
        UI_LOG("[%s] onCreate\n", App_ChatGPT_appName().c_str());
        _GPT_init();

        /* Create Record Button */
        record_btn = lv_btn_create(lv_scr_act());
        lv_obj_set_pos(record_btn, 10, 10);
        lv_obj_set_size(record_btn, 100, 50);
        lv_obj_add_event_cb(record_btn, record_btn_event_handler, LV_EVENT_CLICKED, NULL);

        /* Create Record Label */
        record_label = lv_label_create(record_btn);
        lv_label_set_text(record_label, "Record");
        lv_obj_center(record_label);

        /* Create Play Button */
        play_btn = lv_btn_create(lv_scr_act());
        lv_obj_set_pos(play_btn, 120, 10);
        lv_obj_set_size(play_btn, 100, 50);
        lv_obj_add_event_cb(play_btn, play_btn_event_handler, LV_EVENT_CLICKED, NULL);

        /* Create Play Label */
        play_label = lv_label_create(play_btn);
        lv_label_set_text(play_label, "Play");
        lv_obj_center(play_label);
    }

    /**
     * @brief Called repeatedly, end this function ASAP! or the App management will be affected
     * If the thing you want to do takes time, try to create a task or lvgl timer to handle them.
     * Try to use millis() instead of delay() here
     * 
     */
    void App_ChatGPT_onLoop()
    {
    }

    /**
     * @brief Called when App is about to be destroyed
     * Please remember to release the resources like lvgl timers in this function
     * 
     */
    void App_ChatGPT_onDestroy()
    {
        UI_LOG("[%s] onDestroy\n", App_ChatGPT_appName().c_str());
    }

    /**
     * @brief Launcher will pass the BSP pointer through this function before onCreate
     * 
     */
    void App_ChatGPT_getBsp(void* bsp)
    {
        device = (CHAPPIE*)bsp;
    }

}

#endif
