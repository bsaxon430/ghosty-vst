"""The network that paints the hole in: LaMa (Suvorov et al., github.com/advimman/lama, Apache-2.0), through its TorchScript
export big-lama.pt (github.com/enesmsahin/simple-lama-inpainting, release v0.1.0, Apache-2.0;
sha256 7ba7aa7ac37a4d41fdbbeba3a2af7ead18058552997e3a3cd1a3b2210c9e6b4c). The model is a tool used here, like the
enlarging model: it is not part of the plug-in and is not kept in this repository. Give its path in LAMA_MODEL."""
import os
import cv2, numpy as np, torch

_model = None


def model():
    global _model
    if _model is None:
        torch.set_num_threads(int(os.environ.get("BOAT_THREADS", "2")))
        _model = torch.jit.load(os.environ["LAMA_MODEL"], map_location="cpu").eval()
    return _model


def raw(img_bgr, mask):
    """The network's picture for the whole of img (mask: 255 where it must invent)."""
    h, w = mask.shape
    ph, pw = (8 - h % 8) % 8, (8 - w % 8) % 8
    img = cv2.copyMakeBorder(img_bgr, 0, ph, 0, pw, cv2.BORDER_REFLECT)
    m = cv2.copyMakeBorder(mask, 0, ph, 0, pw, cv2.BORDER_REFLECT)
    x = torch.from_numpy(cv2.cvtColor(img, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0).permute(2, 0, 1)[None]
    mm = torch.from_numpy((m > 127).astype(np.float32))[None, None]
    with torch.inference_mode():
        out = model()(x, mm)
    o = (out[0].permute(1, 2, 0).numpy() * 255.0).clip(0, 255).astype(np.uint8)[:h, :w]
    return cv2.cvtColor(o, cv2.COLOR_RGB2BGR)


def feather(mask, px=4.0):
    """0 outside the mask, rising to 1 a few pixels inside it."""
    d = cv2.distanceTransform((mask > 127).astype(np.uint8), cv2.DIST_L2, 5)
    return np.clip(d / px, 0.0, 1.0).astype(np.float32)


def window(mask, shape, margin=0.9, least=160):
    ys, xs = np.where(mask > 127)
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    mx, my = max(least, int((x1 - x0) * margin)), max(least, int((y1 - y0) * margin))
    return max(0, x0 - mx), max(0, y0 - my), min(shape[1], x1 + mx), min(shape[0], y1 + my)


def inpaint(img_bgr, mask, whole=True):
    """img with the masked part painted in. whole: the network sees the whole frame; otherwise a window round the mask."""
    if whole: x0, y0, x1, y1 = 0, 0, img_bgr.shape[1], img_bgr.shape[0]
    else: x0, y0, x1, y1 = window(mask, img_bgr.shape)
    fill = raw(img_bgr[y0:y1, x0:x1], mask[y0:y1, x0:x1])
    a = feather(mask[y0:y1, x0:x1])[..., None]
    out = img_bgr.copy()
    out[y0:y1, x0:x1] = (img_bgr[y0:y1, x0:x1] * (1 - a) + fill * a + 0.5).astype(np.uint8)
    return out
