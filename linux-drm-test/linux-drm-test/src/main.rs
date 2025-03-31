mod card;
use std::time::{Duration, Instant};

use card::Card;

mod display;
use display::DrawingBoard;

use drm::control::Device as ControlDevice;

use embedded_graphics::draw_target::DrawTarget;
use embedded_graphics::pixelcolor::Rgb888;
use embedded_graphics::prelude::RgbColor;

const SCREEN_WIDTH: i32 = 1920;
const SCREEN_HEIGHT: i32 = 1080;
const INIT_X: i32 = 40;
const INIT_Y: i32 = 40;
const RECT_SIZE: u32 = 60;
const ITERATIONS: usize = 5000;

fn flush(gpu: &Card, fb: &drm::control::framebuffer::Handle) {
    gpu.set_crtc(
        gpu.crtc.unwrap().handle(),
        Some(*fb),
        (0, 0),
        &[gpu.connector.as_ref().unwrap().handle()],
        Some(gpu.mode.unwrap()),
    )
    .expect("Failed to set CRTC");
}

fn main() {
    let mut gpu = Card::new("/dev/dri/card1");
    gpu.init();
    println!("{:#?}", gpu.mode.unwrap());

    // Create a dumb buffer
    let mut db = gpu
        .create_dumb_buffer(
            (
                gpu.disp_width.unwrap().into(),
                gpu.disp_height.unwrap().into(),
            ),
            gpu.format.unwrap(),
            32,
        )
        .expect("Could not create dumb buffer");

    // Create an FB:
    let fb = gpu
        .add_framebuffer(&db, 24, 32)
        .expect("Could not create FB");

    // Create a dumb buffer mapping
    let mut map = gpu
        .map_dumb_buffer(&mut db)
        .expect("Could not map dumbbuffer");

    let mut board = DrawingBoard::new(map);
    board.disp.clear(Rgb888::BLACK).ok();

    let start_all = Instant::now();
    let mut fifty_paint_duration = Duration::new(0, 0);
    let mut final_paint_duration = Duration::new(0, 0);
    let mut fifty_flush_duration = Duration::new(0, 0);
    let mut final_flush_duration = Duration::new(0, 0);
    let mut flush_times = 0;

    for _ in 0..ITERATIONS {
        if board.latest_pos.x + RECT_SIZE as i32 + 20 > SCREEN_WIDTH {
            board.latest_pos.x = INIT_X;
            board.latest_pos.y += RECT_SIZE as i32 + 20;
        } else {
            board.latest_pos.x += RECT_SIZE as i32 + 20;
        }

        if board.latest_pos.y + RECT_SIZE as i32 + 100 > SCREEN_HEIGHT {
            board.latest_pos.x = INIT_X;
            board.latest_pos.y = INIT_Y;
        }

        let start_paint = Instant::now();
        board.paint();
        let paint_duration = start_paint.elapsed();
        fifty_paint_duration += paint_duration;
        
        let start_flush = Instant::now();
        flush(&gpu, &fb);
        let flush_duration = start_flush.elapsed();
        fifty_flush_duration += flush_duration;

        flush_times += 1;

        if flush_times == 50 {
            final_paint_duration += fifty_paint_duration;
            final_flush_duration += fifty_flush_duration;
            println!(
                "50 times paint cost {:?}, 50 times flush cost {:?}",
                fifty_paint_duration, fifty_flush_duration
            );
            fifty_paint_duration = Duration::new(0, 0);
            fifty_flush_duration = Duration::new(0, 0);
            flush_times = 0;
        }
    }

    let duration_all = start_all.elapsed();
    let fps = ITERATIONS as f64 / duration_all.as_secs_f64();
    println!(
        "draw {} times cost {:?}, FPS: {:.2}, total flush time: {:?}, average flush time: {:?}, total paint time: {:?}, average paint time: {:?}",
        ITERATIONS,
        duration_all,
        fps,
        final_flush_duration,
        final_flush_duration / ITERATIONS as u32,
        final_paint_duration,
        final_paint_duration / ITERATIONS as u32
    );

    gpu.destroy_framebuffer(fb).unwrap();
    // gpu.destroy_dumb_buffer(db).unwrap();
    
    // 保持程序运行
    loop {}
}