/* Copyright (c) [2023] [Syswonder Community]
 *   [Ruxos] is licensed under Mulan PSL v2.
 *   You can use this software according to the terms and conditions of the Mulan PSL v2.
 *   You may obtain a copy of Mulan PSL v2 at:
 *               http://license.coscl.org.cn/MulanPSL2
 *   THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *   See the Mulan PSL v2 for more details.
 */

use embedded_graphics::pixelcolor::Rgb888;
use embedded_graphics::prelude::{RgbColor, Size};
use embedded_graphics::{draw_target::DrawTarget, prelude::OriginDimensions};
use log::info;

use std::os::arceos::api::display as api;
use std::time::{Duration, Instant};

pub struct Display {
    size: Size,
    fb: &'static mut [u8],
    total_flush_duration: Duration, // 总耗时
    flush_count: u32,               // flush计数器
    final_flush_duration: Duration,
}

impl Display {
    pub fn new() -> Self {
        let start = Instant::now();

        let info = api::ax_framebuffer_info();

        let duration = start.elapsed();

        info!("get framebuffer cost {:?}", duration);

        let fb =
            unsafe { core::slice::from_raw_parts_mut(info.fb_base_vaddr as *mut u8, info.fb_size) };
        let size = Size::new(info.width, info.height);
        Self {
            size,
            fb,
            flush_count: 0,
            total_flush_duration: Duration::new(0, 0),
            final_flush_duration: Duration::new(0, 0),
        }
    }

    pub fn flush(&mut self) {
        let start = Instant::now();

        api::ax_framebuffer_flush();

        let duration = start.elapsed();
        self.total_flush_duration += duration;
        self.flush_count += 1;

        if self.flush_count == 50 {
            info!(
                "average flush time for 50 flushes: {:?}",
                self.total_flush_duration / 50
            );
            self.flush_count = 0;
            self.final_flush_duration += self.total_flush_duration;
            self.total_flush_duration = Duration::new(0, 0);
        }
    }

    pub fn final_flush_duration(&mut self) -> Duration {
        self.final_flush_duration
    }
}

impl OriginDimensions for Display {
    fn size(&self) -> Size {
        self.size
    }
}

impl DrawTarget for Display {
    type Color = Rgb888;
    type Error = core::convert::Infallible;

    fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = embedded_graphics::Pixel<Self::Color>>,
    {
        pixels.into_iter().for_each(|px| {
            let idx = (px.0.y * self.size.width as i32 + px.0.x) as usize * 4;
            if idx + 2 >= self.fb.len() {
                return;
            }
            self.fb[idx] = px.1.b();
            self.fb[idx + 1] = px.1.g();
            self.fb[idx + 2] = px.1.r();
        });

        Ok(())
    }
}
