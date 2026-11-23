(() => {
  'use strict';
  const byId = id => document.getElementById(id);
  const zone = 'America/New_York';
  const dateKey = new Intl.DateTimeFormat('en', {timeZone: zone, year: 'numeric', month: '2-digit', day: '2-digit'});
  const dateLabel = new Intl.DateTimeFormat('en-US', {timeZone: zone, weekday: 'long', month: 'long', day: 'numeric'});
  const weatherTime = new Intl.DateTimeFormat('en-US', {timeZone: zone, month: 'short', day: 'numeric', hour: 'numeric', minute: '2-digit', timeZoneName: 'short'});
  const dayPrefix = 'luna.desk.day.v1.';
  const weatherKey = 'luna.desk.nyc-weather.v1';
  const pageSize = 3;
  const memory = new Map();
  let storageOK = true, page = 0, day = '', lastAction = null, notice = '';
  let data = {tasks: [], imported: ''};
  let report = null, lastWeatherFetch = 0, loadingWeather = false, weatherFailed = false;

  function today() {
    const p = Object.fromEntries(dateKey.formatToParts(new Date()).map(v => [v.type, v.value]));
    return p.year + '-' + p.month + '-' + p.day;
  }
  function read(key) {
    if (!storageOK && memory.has(key)) return memory.get(key);
    try {
      const value = localStorage.getItem(key);
      if (value) return JSON.parse(value);
    } catch { storageOK = false; }
    return memory.get(key);
  }
  function write(key, value) {
    memory.set(key, value);
    try { localStorage.setItem(key, JSON.stringify(value)); }
    catch { storageOK = false; }
  }
  function sourceURL(value) {
    if (!value) return '';
    if (value === 'japanese.html') return value;
    try { const u = new URL(value); return u.protocol === 'https:' ? u.href : ''; } catch { return ''; }
  }
  function seed() {
    const source = window.LUNA_MORNING;
    if (!source || !/^\d{4}-\d{2}-\d{2}$/.test(source.date) || !Array.isArray(source.tasks)) return null;
    // The included local tasks seed whichever day the demo is given.
    const date = day || today();
    return {date, tasks: source.tasks.filter(t => typeof t?.title === 'string' && t.title.trim()).map((t, i) => ({
      id: 'source-' + date + '-' + i,
      title: t.title.trim(), url: sourceURL(t.url), done: false
    }))};
  }
  function loadDay() {
    const current = today();
    if (day !== current) { lastAction = null; notice = ''; page = 0; }
    day = current;
    const saved = read(dayPrefix + day);
    data = saved && Array.isArray(saved.tasks) ? saved : {tasks: [], imported: ''};
    const snapshot = seed();
    if (snapshot && data.imported !== day && !data.tasks.length) {
      const known = new Set(data.tasks.map(t => t.id));
      data.tasks = snapshot.tasks.filter(t => !known.has(t.id)).concat(data.tasks);
      data.imported = day;
      write(dayPrefix + day, data);
    }
  }
  function emit() { window.dispatchEvent(new Event('luna-desk-change')); }
  function mutate(fn) {
    loadDay();
    fn();
    write(dayPrefix + day, data);
    render();
    emit();
  }
  function el(tag, text) {
    const node = document.createElement(tag);
    node.textContent = text;
    return node;
  }
  function encourage(title, completed) {
    if (completed === 1) return 'First star of the day, Tanya. “' + title + '” is done.';
    if (completed === data.tasks.length) return 'Every item has a star. Take a breath, Tanya.';
    return 'One more star for your sky. “' + title + '” is done.';
  }
  function toggleTask(id) {
    mutate(() => {
      const task = data.tasks.find(t => t.id === id);
      if (!task) return;
      lastAction = {id, before: !!task.done};
      task.done = !task.done;
      notice = task.done ? encourage(task.title, data.tasks.filter(t => t.done).length) : 'Back on your list.';
    });
    const strip = byId('workMessage').parentElement;
    strip.classList.remove('celebrate');
    if (data.tasks.find(t => t.id === id)?.done) { void strip.offsetWidth; strip.classList.add('celebrate'); starFor(id); }
  }
  // A ticked task turns into a little star that flies up to the progress count.
  function starFor(id) {
    const panel = byId('workPanel');
    const index = data.tasks.findIndex(t => t.id === id) - page * pageSize;
    const box = byId('workTasks').querySelectorAll('input')[index], goal = byId('workProgress');
    if (!panel.open || !box || !goal || !box.animate) return;
    const still = matchMedia('(prefers-reduced-motion: reduce)').matches;
    const from = box.getBoundingClientRect(), to = goal.getBoundingClientRect(), frame = panel.getBoundingClientRect();
    const x0 = from.left + from.width / 2 - frame.left, y0 = from.top + from.height / 2 - frame.top;
    const dx = to.left + to.width / 2 - frame.left - x0, dy = to.top + to.height / 2 - frame.top - y0;
    const layer = el('div', ''); layer.className = 'star-layer'; layer.setAttribute('aria-hidden', 'true');
    panel.append(layer);
    const star = el('span', '★'); star.className = 'fly-star';
    star.style.left = x0 + 'px'; star.style.top = y0 + 'px'; layer.append(star);
    const flight = star.animate([
      {transform: 'translate(-50%,-50%) scale(.2) rotate(-40deg)', opacity: 0},
      {transform: 'translate(-50%,-50%) scale(1.9) rotate(0deg)', opacity: 1, offset: .22},
      {transform: 'translate(calc(-50% + ' + dx * .45 + 'px), calc(-50% + ' + (dy * .45 - 46) + 'px)) scale(1.5) rotate(140deg)', opacity: 1, offset: .62},
      {transform: 'translate(calc(-50% + ' + dx + 'px), calc(-50% + ' + dy + 'px)) scale(.5) rotate(360deg)', opacity: .1}
    ], {duration: still ? 1 : 1050, easing: 'cubic-bezier(.3,.7,.3,1)'});
    if (!still) for (let i = 0; i < 7; i++) {
      const bit = el('span', i % 2 ? '✦' : '·'); bit.className = 'fly-spark';
      bit.style.left = x0 + 'px'; bit.style.top = y0 + 'px'; layer.append(bit);
      const a = Math.PI * 2 * i / 7 + .4, r = 26 + 9 * (i % 3);
      bit.animate([{transform: 'translate(-50%,-50%) scale(.4)', opacity: 1},
        {transform: 'translate(calc(-50% + ' + Math.cos(a) * r + 'px), calc(-50% + ' + Math.sin(a) * r + 'px)) scale(1.1)', opacity: 0}],
        {duration: 620, delay: 60, easing: 'ease-out', fill: 'both'});
    }
    const done = () => { layer.remove(); goal.classList.remove('gained'); void goal.offsetWidth; goal.classList.add('gained'); };
    flight.onfinish = done; flight.oncancel = () => layer.remove();
  }
  function renderCarry() {
    const source = seed();
    const area = byId('workCarry'), list = byId('carryList');
    list.replaceChildren();
    if (!source || source.date >= day) { area.hidden = true; return; }
    const ids = new Set(data.tasks.map(t => t.id));
    const available = source.tasks.filter(t => !ids.has(t.id));
    area.hidden = !available.length;
    byId('carryTitle').textContent = 'Choose from the ' + source.date + ' checklist';
    available.forEach(t => {
      const item = el('li', ''), button = el('button', '+ ' + t.title);
      button.type = 'button'; button.className = 'utility';
      button.onclick = () => mutate(() => {
        if (data.tasks.some(x => x.id === t.id)) return;
        data.tasks.push({...t}); page = Math.floor((data.tasks.length - 1) / pageSize);
        lastAction = null; notice = 'Added to today’s checklist.';
      });
      item.append(button); list.append(item);
    });
  }
  function render() {
    const count = data.tasks.length, checked = data.tasks.filter(t => t.done).length;
    page = Math.min(Math.max(page, 0), Math.max(0, Math.ceil(count / pageSize) - 1));
    byId('workDate').textContent = dateLabel.format(new Date()) + ' · New York';
    byId('workProgress').textContent = checked + ' / ' + count + ' complete';
    byId('workSource').textContent = (data.imported === day ? 'Morning snapshot · ' : '') +
      (storageOK ? 'Updates saved in this browser.' : 'Storage unavailable. Updates last for this session.');
    const ul = byId('workTasks'); ul.replaceChildren();
    if (!count) ul.append(el('li', 'Add an item to begin today’s list.'));
    data.tasks.slice(page * pageSize, (page + 1) * pageSize).forEach(t => {
      const row = el('li', ''), label = el('label', ''), input = document.createElement('input');
      input.type = 'checkbox'; input.checked = !!t.done;
      input.onchange = () => { toggleTask(t.id); byId('workTasks').querySelectorAll('input')[data.tasks.findIndex(v => v.id === t.id) % pageSize]?.focus(); };
      label.append(input, el('span', t.title)); row.append(label);
      const url = sourceURL(t.url);
      if (url) {
        const local = url === 'japanese.html';
        const link = el('a', local ? 'Open notebook →' : 'Source ↗');
        link.href = url; if (!local) { link.target = '_blank'; link.rel = 'noopener noreferrer'; }
        link.setAttribute('aria-label','Open source for '+t.title); row.append(link);
      }
      ul.append(row);
    });
    const pages = Math.ceil(count / pageSize);
    byId('workPages').hidden = pages < 2;
    byId('workPage').textContent = (page + 1) + ' / ' + pages;
    byId('workPrev').disabled = page <= 0; byId('workNext').disabled = page + 1 >= pages;
    byId('workMessage').textContent = notice || (count && checked === count ? 'Every item has a star. Take a breath, Tanya.' : 'One thing at a time, Tanya.');
    byId('workUndo').hidden = !lastAction;
    renderCarry();
  }
  byId('workDesk').onclick = () => { loadDay(); render(); byId('workPanel').showModal(); };
  byId('workPrev').onclick = () => { page -= 1; render(); };
  byId('workNext').onclick = () => { page += 1; render(); };
  byId('workUndo').onclick = () => mutate(() => {
    if (!lastAction) return;
    const task = data.tasks.find(t => t.id === lastAction.id);
    if (task) task.done = lastAction.before;
    lastAction = null; notice = 'Last change undone.';
  });
  byId('workAdd').onsubmit = event => {
    event.preventDefault();
    const field = byId('workNew'), title = field.value.trim();
    if (!title) { field.focus(); return; }
    mutate(() => {
      data.tasks.push({id: 'local-' + crypto.randomUUID(), title: title.slice(0, 220), url: '', done: false});
      page = Math.floor((data.tasks.length - 1) / pageSize);
      lastAction = null; notice = 'Added to today’s checklist.';
    });
    field.value = ''; field.focus();
  };

  function validReport(v) {
    return !!v && typeof v.c === 'number' && Number.isFinite(v.c) && v.c > -100 && v.c < 90 &&
      Number.isFinite(v.at) && v.at > 0 && v.at <= Date.now() + 60000;
  }
  function currentReport() { return validReport(report) && Date.now() - report.at < 2 * 3600000; }
  function weatherView() {
    const available = validReport(report);
    const temp = available ? Math.round(report.c) + '°C / ' + Math.round(report.c * 9 / 5 + 32) + '°F' : '';
    const old = available && (!currentReport() || weatherFailed);
    const short = temp ? ((old ? 'Last ' : '') + temp) : (loadingWeather ? 'Checking…' : 'Unavailable');
    byId('weatherReading').textContent = short;
    byId('weatherOpen').setAttribute('aria-label', 'New York City temperature: ' + short + '. Weather details.');
    byId('weatherDetail').textContent = temp || (loadingWeather ? 'Checking the temperature…' : 'Temperature unavailable');
    let detail = available ? 'Reading for ' + weatherTime.format(new Date(report.at)) + '.' : '';
    if (weatherFailed) detail += (detail ? ' ' : '') + 'Couldn’t reach Open-Meteo. You can refresh to try again.';
    else if (old) detail += ' Showing the last available reading.';
    byId('weatherAt').textContent = detail;
    byId('weatherRefresh').disabled = loadingWeather;
  }
  async function refreshWeather(force) {
    if (loadingWeather || (!force && Date.now() - lastWeatherFetch < 15 * 60000)) return;
    loadingWeather = true; weatherView();
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 6500);
    try {
      const url = new URL('https://api.open-meteo.com/v1/forecast');
      url.search = new URLSearchParams({latitude:'40.7128', longitude:'-74.0060', current:'temperature_2m', temperature_unit:'celsius', timeformat:'unixtime'}).toString();
      const result = await fetch(url, {signal:controller.signal, credentials:'omit', referrerPolicy:'no-referrer'});
      if (!result.ok) throw Error('Weather unavailable');
      const json = await result.json();
      const value = {c:json.current?.temperature_2m, at:json.current?.time * 1000};
      if (json.current_units?.temperature_2m !== '°C' || !validReport(value)) throw Error('Invalid weather data');
      report = value; write(weatherKey, value); lastWeatherFetch = Date.now(); weatherFailed = false;
    } catch { weatherFailed = true; }
    finally { clearTimeout(timer); loadingWeather = false; weatherView(); emit(); }
  }
  byId('weatherOpen').onclick = () => { weatherView(); byId('weatherDialog').showModal(); };
  byId('weatherRefresh').onclick = () => refreshWeather(true);
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) return;
    loadDay(); render(); refreshWeather(false);
  });
  window.addEventListener('storage', e => {
    if (e.key === dayPrefix + today() || e.key === null) { lastAction = null; notice = ''; loadDay(); render(); emit(); }
    if (e.key === weatherKey) { report = read(weatherKey); weatherView(); emit(); }
  });
  window.LunaDesk = {
    snapshot() { loadDay(); return {
      titles: data.tasks.slice(0, pageSize).map(t => t.title),
      done: data.tasks.slice(0, pageSize).map(t => !!t.done),
      tempC: currentReport() ? report.c : null
    }; },
    toggle(index) {
      loadDay();
      if (Number.isInteger(index) && index >= 0 && index < pageSize && data.tasks[index]) toggleTask(data.tasks[index].id);
    }
  };
  loadDay(); render();
  report = read(weatherKey); if (!validReport(report)) report = null;
  refreshWeather(false);
  setInterval(() => { if (!document.hidden) { if (day !== today()) { loadDay(); render(); emit(); } refreshWeather(false); } }, 60000);
})();
