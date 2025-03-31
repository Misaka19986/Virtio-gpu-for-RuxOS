use drm::control::dumbbuffer::DumbMapping;
use embedded_graphics::mono_font::ascii::FONT_10X20;
use embedded_graphics::mono_font::MonoTextStyle;
use embedded_graphics::pixelcolor::Rgb888;
use embedded_graphics::prelude::{Point, Primitive, RgbColor, Size};
use embedded_graphics::primitives::{Circle, PrimitiveStyle, Rectangle, Triangle};
use embedded_graphics::text::{Alignment, Text};
use embedded_graphics::Drawable;
use embedded_graphics::{draw_target::DrawTarget, prelude::OriginDimensions};

const SCREEN_WIDTH: u32 = 1920;
const SCREEN_HEIGHT: u32 = 1080;
const INIT_X: i32 = 40;
const INIT_Y: i32 = 40;
const RECT_SIZE: u32 = 60;

pub struct Display<'a> {
    size: Size,
    fb: DumbMapping<'a>,
}

impl<'a> Display<'a> {
    pub fn new(map: DumbMapping<'a>) -> Display<'a> {
        let size = Size::new(SCREEN_WIDTH, SCREEN_HEIGHT);
        Self { size, fb: map }
    }
}

impl<'a> OriginDimensions for Display<'a> {
    fn size(&self) -> Size {
        self.size
    }
}

impl<'a> DrawTarget for Display<'a> {
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

pub struct DrawingBoard<'a> {
    pub disp: Display<'a>,
    pub latest_pos: Point,
}

impl<'a> DrawingBoard<'a> {
    pub fn new(map: DumbMapping<'a>) -> Self {
        Self {
            disp: Display::new(map),
            latest_pos: Point::new(INIT_X, INIT_Y),
        }
    }

    pub fn paint(&mut self) {
        Rectangle::with_center(self.latest_pos, Size::new(RECT_SIZE, RECT_SIZE))
            .into_styled(PrimitiveStyle::with_stroke(Rgb888::RED, 3))
            .draw(&mut self.disp)
            .ok();
        Circle::new(self.latest_pos + Point::new(-50, -100),30)
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
