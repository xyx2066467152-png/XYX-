"""Optional LCD preview of raw observations. It makes no authorization decision."""
def draw_preview(pipeline, recognizer, observation, config):
    canvas = pipeline.osd_img
    canvas.clear()
    rgb_w, rgb_h = config['rgb_size']
    out_w, out_h = pipeline.get_display_size()
    for box in recognizer.last_boxes:
        x,y,w,h = [int(round(value)) for value in box[:4]]
        x=x*out_w//rgb_w; y=y*out_h//rgb_h
        w=w*out_w//rgb_w; h=h*out_h//rgb_h
        canvas.draw_rectangle(x,y,w,h,color=config['preview_box_color'],
                              thickness=config['preview_line_width'])
        label = observation['type']
        if observation['user_id']:
            label = 'ID={} {:.2f}'.format(observation['user_id'],observation['similarity'])
        canvas.draw_string_advanced(x,y,config['preview_font_size'],label,
                                    color=config['preview_text_color'])
    pipeline.show_image()
