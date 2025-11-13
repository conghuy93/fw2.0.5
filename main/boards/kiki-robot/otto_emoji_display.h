#pragma once

#include <libs/gif/lv_gif.h>

#include "display/lcd_display.h"
#include "otto_emoji_gif.h"

/**
 * @brief Otto机器人GIF表情显示类
 * 继承LcdDisplay，添加GIF表情支持
 */
class OttoEmojiDisplay : public SpiLcdDisplay {
public:
    /**
     * @brief 构造函数，参数与SpiLcdDisplay相同
     */
    OttoEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                     int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                     bool swap_xy);

    virtual ~OttoEmojiDisplay() = default;

    // 重写表情设置方法
    virtual void SetEmotion(const char* emotion) override;

    // 重写聊天消息设置方法
    virtual void SetChatMessage(const char* role, const char* content) override;

    // 音乐信息显示方法
    // TODO: void SetMusicInfo(const char* song_name);

    // 重写状态栏更新方法，禁用低电量弹窗显示
    virtual void UpdateStatusBar(bool update_all = false) override;

    // 重写图片预览方法（继承父类实现）
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override {
        SpiLcdDisplay::SetPreviewImage(std::move(image));
    }

    //切换emoji模式方法
    void SetEmojiMode(bool use_otto_emoji);
    bool IsUsingOttoEmoji() const { return use_otto_emoji_; }

    // UDP Drawing support methods
    void EnableDrawingCanvas(bool enable);
    void ClearDrawingCanvas();
    void DrawPixel(int x, int y, bool state);
    bool IsDrawingCanvasEnabled() const { return drawing_canvas_enabled_; }

private:
    void SetupGifContainer();
    void InitializeDrawingCanvas();
    void CleanupDrawingCanvas();

    lv_obj_t* emotion_gif_;  ///< GIF表情组件
    bool use_otto_emoji_;    ///< 是否使用Otto emoji (true) 还是默认emoji (false)

    // UDP Drawing canvas
    lv_obj_t* drawing_canvas_;       ///< Drawing canvas object
    void* drawing_canvas_buf_;       ///< Canvas buffer
    bool drawing_canvas_enabled_;    ///< Is drawing mode enabled

    // 表情映射
    struct EmotionMap {
        const char* name;
        const lv_img_dsc_t* gif;
    };

    static const EmotionMap emotion_maps_[];
};