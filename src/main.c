#include <pebble.h>

Window* my_window = NULL;

//Image Display
BitmapLayer *bitmap_layer = NULL;
GBitmap *gbitmap = NULL;
GBitmapSequence *bitmap_sequence = NULL;
AppTimer *image_sequence_timer = NULL;

//Time Display
char time_string[] = "00:00";  // Make this longer to show AM/PM
TextLayer* time_outline_layer = NULL;
TextLayer* time_text_layer = NULL;

//Date Display
char date_string[16];
TextLayer* date_outline_layer = NULL;
TextLayer* date_text_layer = NULL;

bool animating = true;//don't allow new animation to be queued by taphandler

static const char *const dname[7] =
{"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static void sequence_update(void *context) {
  uint32_t delay_ms = 0;
  
  time_t start_time = 0;
  uint16_t start_time_ms = 0;
  time_ms(&start_time, &start_time_ms);


  if (gbitmap_sequence_update_bitmap_next_frame(bitmap_sequence, gbitmap, &delay_ms)) {
    bitmap_layer_set_bitmap(bitmap_layer, gbitmap);
    layer_mark_dirty(bitmap_layer_get_layer(bitmap_layer));
    time_t end_time = 0;
    uint16_t end_time_ms = 0;
    time_ms(&end_time, &end_time_ms);

    // subtract decoding time from delay
    uint32_t decode_ms = (end_time - start_time) * 1000 + (end_time_ms - start_time_ms);

    // subtract decoding time from delay
    if (decode_ms >= delay_ms) {
      delay_ms = 1;
    } else {
      delay_ms -= decode_ms;
    }

    image_sequence_timer = app_timer_register(delay_ms, sequence_update, NULL);
  } else {
    animating = false;
    light_enable(false);
  }
}

static void load_sequence(uint32_t resource_id) {
  if (image_sequence_timer) {
    app_timer_cancel(image_sequence_timer);
  }
  // free heap allocated resources
  if (bitmap_sequence) {
    gbitmap_sequence_destroy(bitmap_sequence);
    bitmap_sequence = NULL;
  }
  // Load the GBitmapSequence Data (APNG for this demo)
  bitmap_sequence = gbitmap_sequence_create_with_resource(resource_id);

  // Set loop count to 12
  //gbitmap_sequence_set_play_count(bitmap_sequence, 12);

  // Setup the GBitmap for the window
  if (gbitmap) {
    gbitmap_destroy(gbitmap);
    gbitmap = NULL;
  }
  gbitmap = gbitmap_create_blank(gbitmap_sequence_get_bitmap_size(bitmap_sequence), 
      GBitmapFormat8Bit);

  // load first frame and schedule timer
  image_sequence_timer = app_timer_register(1, sequence_update, NULL);
}

void tap_handler(AccelAxisType axis, int32_t direction){
  if(!animating) {
    light_enable(true);
    animating = true;
    gbitmap_sequence_restart(bitmap_sequence);
    // load first frame and schedule timer
    image_sequence_timer = app_timer_register(1, sequence_update, NULL);
  }
}

void tick_handler(struct tm *tick_time, TimeUnits units_changed){
  if (units_changed & DAY_UNIT) {
    time_t current_time = time(NULL);
    struct tm *current_tm = localtime(&current_time);
    //strftime(date_string, sizeof(date_string), "%a %-m/%d", localtime(&current_time));
    snprintf(date_string, sizeof(date_string), "%d/%d", 
      current_tm->tm_mon + 1, current_tm->tm_mday);
    layer_mark_dirty(text_layer_get_layer(date_outline_layer));
    layer_mark_dirty(text_layer_get_layer(date_text_layer));
  }

  clock_copy_time_string(time_string,sizeof(time_string));
  layer_mark_dirty(text_layer_get_layer(time_outline_layer));
  layer_mark_dirty(text_layer_get_layer(time_text_layer));
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  int time_yoffset = PBL_IF_RECT_ELSE(136, 130);
  int date_yoffset = PBL_IF_RECT_ELSE(0, 4);
  int bitmap_yoffset = PBL_IF_RECT_ELSE(-4, -10);
  int bitmap_xoffset = PBL_IF_RECT_ELSE(4, 4);
  
  //Add layers from back to front (background first)

  //Create bitmap layer for background image
  bitmap_layer = bitmap_layer_create(GRect(
        bounds.origin.x + bitmap_xoffset, 
        bounds.origin.y + bitmap_yoffset, 
        bounds.size.w, bounds.size.h));

  //Enable alpha blending for bitmap layer
  bitmap_layer_set_compositing_mode(bitmap_layer, GCompOpSet);
  //Add bitmap_layer to window layer
  layer_add_child(window_layer, bitmap_layer_get_layer(bitmap_layer));

  //Setup the time outline display
  time_outline_layer = text_layer_create(GRect(0, time_yoffset, bounds.size.w + 14, 30));
  text_layer_set_text(time_outline_layer, time_string);
	text_layer_set_font(time_outline_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_BOXY_OUTLINE_30)));
  text_layer_set_text_alignment(time_outline_layer, GTextAlignmentCenter);
  text_layer_set_background_color(time_outline_layer, GColorClear);
  text_layer_set_text_color(time_outline_layer, GColorBlack);
  
  //Add clock text second
  layer_add_child(window_layer, text_layer_get_layer(time_outline_layer));

  //Setup the time display
  time_text_layer = text_layer_create(GRect(0, time_yoffset, bounds.size.w + 14, 30));
  text_layer_set_text(time_text_layer, time_string);
	text_layer_set_font(time_text_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_BOXY_TEXT_30)));
  text_layer_set_text_alignment(time_text_layer, GTextAlignmentCenter);
  text_layer_set_background_color(time_text_layer, GColorClear);
  text_layer_set_text_color(time_text_layer, GColorWhite);
  
  //Add clock text second
  layer_add_child(window_layer, text_layer_get_layer(time_text_layer));

  //date outline
  date_outline_layer = text_layer_create(GRect(0, date_yoffset, bounds.size.w, 18));
  text_layer_set_text(date_outline_layer, date_string);
	text_layer_set_font(date_outline_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_BOXY_OUTLINE_18)));
  text_layer_set_text_alignment(date_outline_layer, GTextAlignmentCenter);
  text_layer_set_background_color(date_outline_layer, GColorClear);
  text_layer_set_text_color(date_outline_layer, GColorBlack);

  layer_add_child(window_layer, text_layer_get_layer(date_outline_layer));
  
  //date text
  date_text_layer = text_layer_create(GRect(0, date_yoffset, bounds.size.w, 18));
  text_layer_set_text(date_text_layer, date_string);
	text_layer_set_font(date_text_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_BOXY_TEXT_18)));
  text_layer_set_text_alignment(date_text_layer, GTextAlignmentCenter);
  text_layer_set_background_color(date_text_layer, GColorClear);
  text_layer_set_text_color(date_text_layer, GColorWhite);

  layer_add_child(window_layer, text_layer_get_layer(date_text_layer));

  //Force time update
  time_t current_time = time(NULL);
  struct tm *current_tm = localtime(&current_time);
  tick_handler(current_tm, DAY_UNIT);

  //Setup tick time handler
  tick_timer_service_subscribe((MINUTE_UNIT), tick_handler);
  
  //Setup tap service
  accel_tap_service_subscribe(tap_handler);

  //Turn on the light
  light_enable(true);

  //Load initial bitmap sequence
  load_sequence(RESOURCE_ID_IMAGE_1);
}

static void window_unload(Window *window) {
  gbitmap_destroy(gbitmap);
}

void handle_init(void) { 
  my_window = window_create();
  window_set_background_color(my_window, GColorBlack);
  window_set_window_handlers(my_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });

  window_stack_push(my_window, false);
}

void handle_deinit(void) {
    bitmap_layer_destroy(bitmap_layer);
	  window_destroy(my_window);
}


int main(void) {
	  handle_init();
	  app_event_loop();
	  handle_deinit();
}
