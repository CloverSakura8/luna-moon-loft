(() => {
  'use strict';
  // Moon photobooth album. Pictures arrive from the CoreS3 over USB and are kept only
  // in this browser (IndexedDB); nothing is uploaded anywhere.
  const byId = id => document.getElementById(id);
  const KEEP = 24, SCALE = 3;
  const dbReady = new Promise((resolve, reject) => {
    const req = indexedDB.open('luna-photobooth', 1);
    req.onupgradeneeded = () => req.result.createObjectStore('photos', {keyPath: 'id'});
    req.onsuccess = () => resolve(req.result); req.onerror = () => reject(req.error);
  });
  async function tx(mode, fn) {
    const db = await dbReady;
    return new Promise((resolve, reject) => {
      const t = db.transaction('photos', mode), r = fn(t.objectStore('photos'));
      t.oncomplete = () => resolve(r?.result); t.onerror = t.onabort = () => reject(t.error);
    });
  }
  const sprite = src => new Promise(resolve => { const i = new Image(); i.onload = () => resolve(i); i.onerror = () => resolve(null); i.src = src; });
  const art = Promise.all([sprite('assets/moon-loft/celebrate_cat.png'), sprite('assets/moon-loft/moon_stamp.png')]);
  function star(g, x, y, r, color) {
    g.fillStyle = color; g.beginPath();
    for (let i = 0; i < 8; i++) { const a = Math.PI / 4 * i, d = i % 2 ? r * .38 : r; g.lineTo(x + Math.sin(a) * d, y - Math.cos(a) * d); }
    g.closePath(); g.fill();
  }
  // Turn the camera's 320x240 RGB565 picture into a framed keepsake PNG.
  async function compose(frame) {
    const small = document.createElement('canvas'); small.width = frame.width; small.height = frame.height;
    const sg = small.getContext('2d'), image = sg.createImageData(frame.width, frame.height), b = frame.bytes;
    for (let i = 0, o = 0; i + 1 < b.length; i += 2, o += 4) {
      const v = (b[i] << 8) | b[i + 1];
      image.data[o] = (v >> 11) * 255 / 31; image.data[o + 1] = ((v >> 5) & 63) * 255 / 63; image.data[o + 2] = (v & 31) * 255 / 31; image.data[o + 3] = 255;
    }
    sg.putImageData(image, 0, 0);
    const W = frame.width * SCALE, H = frame.height * SCALE, pad = 54, foot = 96;
    const card = document.createElement('canvas'); card.width = W + pad * 2; card.height = H + pad + foot;
    const g = card.getContext('2d');
    g.fillStyle = '#432a43'; g.fillRect(0, 0, card.width, card.height);
    g.fillStyle = '#ffda8e'; g.fillRect(pad - 8, pad - 8, W + 16, H + 16);
    g.fillStyle = '#fff1da'; g.fillRect(pad - 4, pad - 4, W + 8, H + 8);
    g.imageSmoothingEnabled = true; g.imageSmoothingQuality = 'high';
    g.drawImage(small, pad, pad, W, H);
    for (let i = 0; i < 16; i++) star(g, 40 + i * (card.width - 80) / 15, 26, i % 2 ? 9 : 13, i % 3 ? '#ffda8e' : '#ffc3d8');
    const [cat, stamp] = await art;
    g.imageSmoothingEnabled = false;
    if (cat) g.drawImage(cat, card.width - pad - 250, H + pad - 196, 288, 288);
    if (stamp) g.drawImage(stamp, pad, H + pad + 14, 72, 72);
    g.fillStyle = '#fff1da'; g.font = '600 34px "Iowan Old Style", Palatino, Georgia, serif'; g.textBaseline = 'middle';
    g.fillText('Tanya’s Moon Loft', pad + 90, H + pad + 38);
    g.fillStyle = '#e3d4f3'; g.font = '500 22px "Avenir Next", system-ui, sans-serif';
    g.fillText(new Date().toLocaleDateString('en-GB', {day: 'numeric', month: 'long', year: 'numeric'}) + ' · New York', pad + 90, H + pad + 72);
    return new Promise(resolve => card.toBlob(resolve, 'image/png'));
  }
  let latestURL = null;
  async function render() {
    const photos = (await tx('readonly', s => s.getAll()) || []).sort((a, b) => b.date - a.date);
    const strip = byId('boothStrip'); strip.replaceChildren();
    byId('boothLatest').hidden = !photos.length;
    if (latestURL) URL.revokeObjectURL(latestURL);
    if (photos.length) {
      latestURL = URL.createObjectURL(photos[0].blob);
      byId('boothImage').src = latestURL; byId('boothDownload').href = latestURL;
      byId('boothDownload').download = 'moon-photobooth-' + photos[0].id + '.png';
    }
    for (const photo of photos.slice(1)) {
      const li = document.createElement('li'), link = document.createElement('a'), img = document.createElement('img');
      const url = URL.createObjectURL(photo.blob);
      img.src = url; img.alt = 'Photobooth picture from ' + new Date(photo.date).toLocaleString(); img.onload = () => {};
      link.href = url; link.download = 'moon-photobooth-' + photo.id + '.png'; link.append(img); li.append(link); strip.append(li);
    }
    for (const old of photos.slice(KEEP)) await tx('readwrite', s => s.delete(old.id));
  }
  function ready() { byId('boothStatus').textContent = window.LunaBoard ? 'CoreS3 connected.' : 'Connect the CoreS3 in Settings to save photos here.'; byId('boothSnap').disabled = !window.LunaBoard; }
  byId('boothOpen').onclick = () => { ready(); render().catch(() => {}); byId('boothDialog').showModal(); };
  byId('boothSnap').onclick = () => { if (window.LunaBoard) { window.LunaBoard.snap(); byId('boothStatus').textContent = 'Look at the CoreS3: 3, 2, 1…'; } };
  window.LunaBooth = {
    async add(frame) {
      const blob = await compose(frame);
      await tx('readwrite', s => s.put({id: frame.id + '-' + Date.now().toString(36), date: Date.now(), blob}));
      await render(); ready(); byId('boothStatus').textContent = 'Saved to your album.';
      if (!byId('boothDialog').open && !document.querySelector('dialog[open]')) byId('boothDialog').showModal();
    },
    compose
  };
})();
