#include <pebble.h>

typedef struct {
  char line_id[4];
  char time[8];
  char dest[16];
  char note1_id[4];
  char note2[64];
  bool is_active;
  bool is_blinking;
  bool blink_visible;
  int blink_count;
} TrainSlot;

static Window *s_main_window;
static Layer *s_canvas_layer;
static Layer *s_content_layer;
static GFont s_font_18, s_font_18_bold;
static TrainSlot s_slots[3];
static char s_station_name[32];
static AppTimer *s_blink_timer = NULL;

// 追加：ボタン状態管理
static int s_station_idx = 0;   // 0, 1, 2... (案A: 同一ファイル内の#数)
static int s_time_offset = 0;  // 0:最短, 1:一本後...
static int s_max_stations = 1; // JSから受信する駅の総数
static bool s_should_animate = false; // バウンス実行判定フラグ

static void animate_layer(Layer *layer, GRect start, GRect end, int duration, int delay) {
  PropertyAnimation *prop_anim = property_animation_create_layer_frame(layer, &start, &end);
  Animation *anim = property_animation_get_animation(prop_anim);
  animation_set_duration(anim, duration);
  animation_set_delay(anim, delay);
  animation_set_curve(anim, AnimationCurveEaseOut); // 停止時に滑らかにする
  animation_schedule(anim);
}

static uint32_t get_line_resource_id(char *id_str) {
  if (strcmp(id_str, "1") == 0) return RESOURCE_ID_IMG_KEIHIN;
  if (strcmp(id_str, "2") == 0) return RESOURCE_ID_IMG_UTLINE;
  if (strcmp(id_str, "3") == 0) return RESOURCE_ID_IMG_SSLINE;
  if (strcmp(id_str, "4") == 0) return RESOURCE_ID_IMG_SAIKYO;
  return 0;
}

static uint32_t get_note1_resource_id(char *id_str) {
  if (strcmp(id_str, "a") == 0) return RESOURCE_ID_IMG_15CARS;
  if (strcmp(id_str, "b") == 0) return RESOURCE_ID_IMG_10CARS;
	if (strcmp(id_str, "c") == 0) return RESOURCE_ID_IMG_RAPID;
	if (strcmp(id_str, "d") == 0) return RESOURCE_ID_IMG_RAPID2;
	if (strcmp(id_str, "e") == 0) return RESOURCE_ID_IMG_RAPID3;
  return 0;
}

// JSへ現在の状態（駅IDとオフセット）を送信して更新要求
static void request_js_update() {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  if (iter) {
    dict_write_int32(iter, MESSAGE_KEY_KEY_URL_INDEX, s_station_idx);
    // JS側で受け取るための新しいキー（仮に106, 107等を使用）を想定
    // ここでは既存の仕組みを拡張し、JS側で判定させます
    dict_write_int32(iter, MESSAGE_KEY_KEY_TIME_OFFSET, s_time_offset);
    app_message_outbox_send();
  }
}

static void blink_timer_callback(void *data) {
  bool any_blinking = false;
  for (int i = 0; i < 3; i++) {
    if (s_slots[i].is_blinking) {
      s_slots[i].blink_count++;
      s_slots[i].blink_visible = !s_slots[i].blink_visible;
      if (s_slots[i].blink_count >= 10) {
        s_slots[i].is_blinking = false;
        s_slots[i].blink_visible = true;
        request_js_update();
      } else {
        any_blinking = true;
      }
    }
  }
  layer_mark_dirty(s_content_layer);
  if (any_blinking) s_blink_timer = app_timer_register(500, blink_timer_callback, NULL);
  else s_blink_timer = NULL;
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_text_color(ctx, GColorBlack);
  int offset_h = PBL_IF_ROUND_ELSE(16, 0);
  int offset_s = PBL_IF_ROUND_ELSE(22, 0);

  // 1. ヘッダー描画
  graphics_draw_text(ctx, s_station_name, s_font_18, GRect(PBL_IF_ROUND_ELSE(0, 2 + offset_h), 0, PBL_IF_ROUND_ELSE(bounds.size.w, 90), 22), 
                     GTextOverflowModeTrailingEllipsis, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft), NULL);
  
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  static char s_clock_buf[8];
  strftime(s_clock_buf, sizeof(s_clock_buf), "%H:%M", t);
  if (!PBL_IF_ROUND_ELSE(true, false)) { // Rectの場合のみヘッダーに時計を表示
    graphics_draw_text(ctx, s_clock_buf, s_font_18_bold, GRect(94 - offset_h, 0, 48, 22), 
                       GTextOverflowModeFill, GTextAlignmentRight, NULL);
  }

  graphics_draw_line(ctx, GPoint(0, 23), GPoint(bounds.size.w, 23));

  // 2. 各スロットの描画
  for (int i = 0; i < 3; i++) {
    int y = 24 + (i * 46);

    // 仕切り線の描画（Roundはスロット2の下にも追加）
    if (i < PBL_IF_ROUND_ELSE(3, 2)) {
      int y_line = y + 43;
      graphics_draw_line(ctx, GPoint(0, y_line), GPoint(bounds.size.w, y_line));
    }
    // Round限定：3番目の仕切り線の下に時計を表示
    if (PBL_IF_ROUND_ELSE(true, false) && i == 2) {
      graphics_draw_text(ctx, s_clock_buf, s_font_18_bold, GRect(0, y + 40, bounds.size.w, 22), 
                         GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    }
    // データがない、または点滅の非表示フェーズならコンテンツ描画をスキップ
    if (!s_slots[i].is_active || strlen(s_slots[i].time) == 0) continue;
    if (s_slots[i].is_blinking && !s_slots[i].blink_visible) continue;

    // --- 以下、コンテンツ描画 ---
    
    // 路線アイコン
    uint32_t res_id = get_line_resource_id(s_slots[i].line_id);
    if (res_id != 0) {
      GBitmap *bmp = gbitmap_create_with_resource(res_id);
      graphics_context_set_compositing_mode(ctx, GCompOpSet);
      graphics_draw_bitmap_in_rect(ctx, bmp, GRect(2 + offset_s, y + 2, PBL_IF_ROUND_ELSE(18, 36), 18));
      gbitmap_destroy(bmp);
    } else {
      graphics_draw_text(ctx, s_slots[i].line_id, s_font_18, GRect(2 + offset_s, y, PBL_IF_ROUND_ELSE(18, 36), 22),
                         GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    }

    // 時刻
    graphics_draw_text(ctx, s_slots[i].time, s_font_18_bold, GRect(PBL_IF_ROUND_ELSE(22, 40) + offset_s, y, 50, 22),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);

    // 行先（修正された座標と幅）
    graphics_draw_text(ctx, s_slots[i].dest, s_font_18, GRect(PBL_IF_ROUND_ELSE(60, 78) + offset_s, y, 60, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    // 備考1
    uint32_t n1_res_id = get_note1_resource_id(s_slots[i].note1_id);
    if (n1_res_id != 0) {
      GBitmap *bmp = gbitmap_create_with_resource(n1_res_id);
      graphics_context_set_compositing_mode(ctx, GCompOpSet);
      graphics_draw_bitmap_in_rect(ctx, bmp, GRect(124 + offset_s, y + 2, 18, 18));
      gbitmap_destroy(bmp);
    } else {
      graphics_draw_text(ctx, s_slots[i].note1_id, s_font_18, GRect(124 + offset_s, y, 18, 22), 
                         GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    }

    // 備考2
    graphics_draw_text(ctx, s_slots[i].note2, s_font_18, GRect(2 + offset_s, y + 20, 140, 22), 
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}
// ボタンクリックハンドラ
static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_time_offset > 0) {
    s_time_offset--;
		s_should_animate = true;
    request_js_update();
  }
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  s_time_offset++;
	s_should_animate = true;
  request_js_update();
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  s_station_idx = (s_station_idx + 1) % s_max_stations;
  s_time_offset = 0;
	s_should_animate = true;
  request_js_update();
}

static void long_click_handler(ClickRecognizerRef recognizer, void *context) {
  s_time_offset = 0;
	s_should_animate = true;
  request_js_update();
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_long_click_subscribe(BUTTON_ID_UP, 500, long_click_handler, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, 500, long_click_handler, NULL);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (units_changed & MINUTE_UNIT) {
    layer_mark_dirty(s_canvas_layer);
    char now_time[8];
    strftime(now_time, sizeof(now_time), "%H:%M", tick_time);
    bool start_timer = false;
    for (int i = 0; i < 3; i++) {
      if (s_slots[i].is_active && strcmp(s_slots[i].time, now_time) == 0 && s_time_offset == 0) {
        s_slots[i].is_blinking = true;
        s_slots[i].blink_count = 0;
        s_slots[i].blink_visible = true;
        start_timer = true;
      }
    }
    if (start_timer && s_blink_timer == NULL) s_blink_timer = app_timer_register(500, blink_timer_callback, NULL);
    else if (!start_timer && !s_blink_timer) request_js_update(); 
  }
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *st_t = dict_find(iterator, MESSAGE_KEY_KEY_STATION);
  if (st_t) strncpy(s_station_name, st_t->value->cstring, sizeof(s_station_name) - 1);
  Tuple *max_t = dict_find(iterator, MESSAGE_KEY_KEY_MAX_STATIONS);
  if (max_t) s_max_stations = (int)max_t->value->int32;
  if (s_max_stations < 1) s_max_stations = 1;
  uint32_t keys_time[] = {MESSAGE_KEY_KEY_S0_TIME, MESSAGE_KEY_KEY_S1_TIME, MESSAGE_KEY_KEY_S2_TIME};
  uint32_t keys_line[] = {MESSAGE_KEY_KEY_S0_LINE, MESSAGE_KEY_KEY_S1_LINE, MESSAGE_KEY_KEY_S2_LINE};
  uint32_t keys_dest[] = {MESSAGE_KEY_KEY_S0_DEST, MESSAGE_KEY_KEY_S1_DEST, MESSAGE_KEY_KEY_S2_DEST};
  uint32_t keys_n1[] = {MESSAGE_KEY_KEY_S0_N1, MESSAGE_KEY_KEY_S1_N1, MESSAGE_KEY_KEY_S2_N1};
  uint32_t keys_n2[] = {MESSAGE_KEY_KEY_S0_N2, MESSAGE_KEY_KEY_S1_N2, MESSAGE_KEY_KEY_S2_N2};
  for (int i = 0; i < 3; i++) {
    Tuple *t_time = dict_find(iterator, keys_time[i]);
    if (t_time && strlen(t_time->value->cstring) > 0) {
      s_slots[i].is_active = true;
      s_slots[i].is_blinking = false;
      strncpy(s_slots[i].time, t_time->value->cstring, sizeof(s_slots[i].time) - 1);
      Tuple *t_line = dict_find(iterator, keys_line[i]);
      if (t_line) strncpy(s_slots[i].line_id, t_line->value->cstring, sizeof(s_slots[i].line_id) - 1);
      Tuple *t_dest = dict_find(iterator, keys_dest[i]);
      if (t_dest) strncpy(s_slots[i].dest, t_dest->value->cstring, sizeof(s_slots[i].dest) - 1);
      Tuple *t_n1 = dict_find(iterator, keys_n1[i]);
      if (t_n1) strncpy(s_slots[i].note1_id, t_n1->value->cstring, sizeof(s_slots[i].note1_id) - 1);
      Tuple *t_n2 = dict_find(iterator, keys_n2[i]);
      if (t_n2) strncpy(s_slots[i].note2, t_n2->value->cstring, sizeof(s_slots[i].note2) - 1);
    } else {
      s_slots[i].is_active = false;
      memset(&s_slots[i], 0, sizeof(TrainSlot));
    }
  }
  if (s_should_animate) {
    GRect bounds = layer_get_bounds(s_canvas_layer);
    animate_layer(s_content_layer, GRect(0, 10, bounds.size.w, bounds.size.h), 
                  GRect(0, 0, bounds.size.w, bounds.size.h), 200, 0);
    s_should_animate = false; // 実行後にリセット
  } else {
    layer_mark_dirty(s_content_layer);
  }
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  s_font_18 = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  s_font_18_bold = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  s_canvas_layer = layer_create(bounds);
  layer_add_child(window_layer, s_canvas_layer);
	s_content_layer = layer_create(bounds);
  layer_set_update_proc(s_content_layer, canvas_update_proc);
  layer_add_child(s_canvas_layer, s_content_layer);
}

static void main_window_unload(Window *window) {
	layer_destroy(s_content_layer);
  layer_destroy(s_canvas_layer);
}

static void init() {
  s_main_window = window_create();
	s_should_animate = true; // 初回ロード時はアニメーションさせる
  window_set_click_config_provider(s_main_window, click_config_provider);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });
  window_stack_push(s_main_window, true);
  app_message_register_inbox_received(inbox_received_callback);
  app_message_open(1024, 128);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void deinit() {
  tick_timer_service_unsubscribe();
  if (s_blink_timer) app_timer_cancel(s_blink_timer);
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}