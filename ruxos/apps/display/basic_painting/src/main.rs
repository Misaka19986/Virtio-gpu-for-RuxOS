/* Copyright (c) [2023] [Syswonder Community]
 *   [Ruxos] is licensed under Mulan PSL v2.
 *   You can use this software according to the terms and conditions of the Mulan PSL v2.
 *   You may obtain a copy of Mulan PSL v2 at:
 *               http://license.coscl.org.cn/MulanPSL2
 *   THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 *   EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 *   MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *   See the Mulan PSL v2 for more details.
 */

#![cfg_attr(feature = "axstd", no_std)]
#![cfg_attr(feature = "axstd", no_main)]

#[cfg(feature = "axstd")]
extern crate axstd as std;

mod display;

use core::time::Duration;
use log::info;
use std::time::Instant;

use self::display::Display;
use embedded_graphics::{
    mono_font::{ascii::FONT_10X20, MonoTextStyle},
    pixelcolor::Rgb888,
    prelude::*,
    primitives::{Circle, PrimitiveStyle, Rectangle, Triangle},
    text::{Alignment, Text},
};

const SCREEN_WIDTH: i32 = 1920;
const SCREEN_HEIGHT: i32 = 1080;
const INIT_X: i32 = 40;
const INIT_Y: i32 = 40;
const RECT_SIZE: u32 = 60;
const ITERATIONS: usize = 5000; // 5000次绘制

pub struct DrawingBoard {
    disp: Display,
    latest_pos: Point,
}

impl Default for DrawingBoard {
    fn default() -> Self {
        Self::new()
    }
}

impl DrawingBoard {
    pub fn new() -> Self {
        Self {
            disp: Display::new(),
            latest_pos: Point::new(INIT_X, INIT_Y),
        }
    }

    fn paint(&mut self) {
        Rectangle::with_center(self.latest_pos, Size::new(RECT_SIZE, RECT_SIZE))
            .into_styled(PrimitiveStyle::with_stroke(Rgb888::RED, 3))
            .draw(&mut self.disp)
            .ok();
        Circle::new(self.latest_pos + Point::new(-50, -100), 30)
            .into_styled(PrimitiveStyle::with_fill(Rgb888::BLUE))
            .draw(&mut self.disp)
            .ok();
        Triangle::new(
            self.latest_pos + Point::new(0, 30),
            self.latest_pos + Point::new(40, 80),
            self.latest_pos + Point::new(-60, 120),
        )
        .into_styled(PrimitiveStyle::with_stroke(Rgb888::GREEN, 3))
        .draw(&mut self.disp)
        .ok();
        let text = "Ruxos";
        Text::with_alignment(
            text,
            self.latest_pos + Point::new(0, 80),
            MonoTextStyle::new(&FONT_10X20, Rgb888::YELLOW),
            Alignment::Center,
        )
        .draw(&mut self.disp)
        .ok();
    }
}

fn test_gpu() {
    let mut board = DrawingBoard::new();
    board.disp.clear(Rgb888::BLACK).unwrap();

    let start_all = Instant::now();

    let mut fifty_paint_duration = Duration::new(0, 0);
    let mut final_paint_duration = Duration::new(0, 0);
    let mut fifty_flush_duration = Duration::new(0, 0);
    let mut final_flush_duration = Duration::new(0, 0);

    let mut flush_times = 0;

    for _ in 0..ITERATIONS {
        if board.latest_pos.x + RECT_SIZE as i32 + 20> SCREEN_WIDTH {
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
        board.disp.flush();
        let flush_duration = start_flush.elapsed();
        fifty_flush_duration += flush_duration;

        flush_times += 1;

        if flush_times == 50 {
            final_paint_duration += fifty_paint_duration;
            final_flush_duration += fifty_flush_duration;
            info!(
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
    info!(
        "draw {} times cost {:?}, FPS: {:.2}, total flush time: {:?}, averge flush time: {:?}, total paint time: {:?}, averge paint time: {:?}",
        ITERATIONS,
        duration_all,
        fps,
        final_flush_duration,
        final_flush_duration / ITERATIONS.try_into().unwrap(),
        final_paint_duration,
        final_paint_duration / ITERATIONS.try_into().unwrap()
    );
}

#[cfg_attr(feature = "axstd", no_mangle)]
fn main() -> ! {
    test_gpu();
    loop {
        core::hint::spin_loop();
    }
}