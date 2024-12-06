/* Copyright (c) [2023] [Syswonder Community]
 *   [Ruxos] is licensed under Mulan PSL v2.
 *   You can use this software according to the terms and conditions of the Mulan PSL v2.
 *   You may obtain a copy of Mulan PSL v2 at:
 *               http://license.coscl.org.cn/MulanPSL2
 *   THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *   See the Mulan PSL v2 for more details.
 */

#![cfg_attr(feature = "axstd", no_std)]
#![cfg_attr(feature = "axstd", no_main)]

#[cfg(feature = "axstd")]
extern crate axstd as std;

mod display;

use self::display::Display;
use embedded_graphics::{
    mono_font::{ascii::FONT_10X20, MonoTextStyle},
    pixelcolor::Rgb888,
    prelude::*,
    primitives::{Circle, PrimitiveStyle, Rectangle, Triangle},
    text::{Alignment, Text},
};
use std::{println, time::Instant};

const INIT_X: i32 = 100;
const INIT_Y: i32 = 100;
const RECT_SIZE: u32 = 50;
const NUM_ITERATIONS: usize = 1000; // 增加渲染次数到1000

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

    let start = Instant::now();

    for _ in 0..NUM_ITERATIONS {
        if board.latest_pos.x + RECT_SIZE as i32 + 20 > 1280 {
            board.latest_pos.x = INIT_X;
            board.latest_pos.y += RECT_SIZE as i32 + 50; // 增加行高，避免重叠
        } else {
            board.latest_pos.x += RECT_SIZE as i32 + 20;
        }

        // 保证不会超过屏幕的垂直方向尺寸
        if board.latest_pos.y + RECT_SIZE as i32 + 120 > 800 {
            board.latest_pos.x = INIT_X;
            board.latest_pos.y = INIT_Y; // 重置到初始位置
        }

        board.paint();
        board.disp.flush();
    }

    let duration = start.elapsed();
    let fps = NUM_ITERATIONS as f64 / duration.as_secs_f64();
    println!(
        "渲染 {} 次耗时: {:?}，FPS: {:.2}",
        NUM_ITERATIONS, duration, fps
    );
}

#[cfg_attr(feature = "axstd", no_mangle)]
fn main() -> ! {
    test_gpu();
    loop {
        core::hint::spin_loop();
    }
}
