use drm::buffer::DrmFourcc;
use drm::control::{connector, crtc, Mode, ResourceHandles};

use drm::control::Device as ControlDevice;
use drm::Device;

impl Device for Card {}
impl ControlDevice for Card {}

#[derive(Debug)]
pub struct Card {
    pub file: std::fs::File,
    pub resource: Option<ResourceHandles>,
    pub connector: Option<connector::Info>,
    pub mode: Option<Mode>,
    pub crtc: Option<crtc::Info>,
    pub format: Option<DrmFourcc>,
    pub disp_width: Option<u16>,
    pub disp_height: Option<u16>,
}

use std::os::unix::io::{AsFd, BorrowedFd};

impl AsFd for Card {
    fn as_fd(&self) -> BorrowedFd<'_> {
        self.file.as_fd()
    }
}

impl Card {
    pub fn new(path: &str) -> Self {
        let mut options = std::fs::OpenOptions::new();
        options.read(true);
        options.write(true);

        let file = options
            .open(path)
            .unwrap_or_else(|_| panic!("Failed to open card: {}", path));

        Card {
            file: file,
            resource: None,
            connector: None,
            mode: None,
            crtc: None,
            format: None,
            disp_width: None,
            disp_height: None,
        }
    }

    pub fn init(&mut self) {
        let resource = self.resource_handles().expect("Failed to get resource");

        let connectors: Vec<connector::Info> = resource
            .connectors()
            .iter()
            .flat_map(|con| self.get_connector(*con, true))
            .collect();
        let crtcs: Vec<crtc::Info> = resource
            .crtcs()
            .iter()
            .flat_map(|crtc| self.get_crtc(*crtc))
            .collect();

        let sel_connector = connectors
            .iter()
            .find(|&i| i.state() == connector::State::Connected)
            .expect("No connected connectors");

        let mode = sel_connector
            .modes()
            .first()
            .expect("No modes found on connector");

        let (disp_width, disp_height) = mode.size();

        let crtc = crtcs
            .iter()
            .find(|&i| i.framebuffer().is_some())
            .expect("No suitable CRTC found");

        let fmt = DrmFourcc::Xrgb8888;
        
        // Update the struct fields
        self.resource = Some(resource);
        self.connector = Some(sel_connector.clone());
        self.mode = Some(*mode);
        self.crtc = Some(crtc.clone());
        self.format = Some(fmt);
        self.disp_width = Some(disp_width);
        self.disp_height = Some(disp_height);
    }
}
