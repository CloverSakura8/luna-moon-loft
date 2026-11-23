(() => {
  'use strict';
  // Inspirational quotes: loaded in Settings, shown one at a time on the quote tile.
  const KEY = 'luna.quotes.v1', LIMIT = 24, LENGTH = 140;
  const defaults = [
    'You are allowed to be a masterpiece and a work in progress.',
    'Small, kind steps still cross the whole sky.',
    'Be the calm one in the room. Then be the brave one.',
    'Read the clause twice. Trust yourself once.',
    'Rest is part of the work, not a reward for it.',
    'Speak slowly. Even the moon takes a month.',
    'Make the thing only you would make.'
  ];
  const byId = id => document.getElementById(id);
  const clean = lines => lines.map(v => String(v).replace(/\s+/g, ' ').trim().slice(0, LENGTH)).filter(Boolean).slice(0, LIMIT);
  let list = defaults, custom = false, at = -1;
  try {
    const saved = JSON.parse(localStorage.getItem(KEY));
    if (Array.isArray(saved) && clean(saved).length) { list = clean(saved); custom = true; }
  } catch {}
  function next() {
    // Start somewhere new each visit, then step through so every press differs.
    if (list.length < 2) at = 0;
    else if (at < 0) at = Math.floor(Math.random() * list.length);
    else at = (at + 1) % list.length;
    const text = byId('quoteText');
    text.textContent = '“' + list[at] + '”';
    text.classList.remove('turn'); void text.offsetWidth; text.classList.add('turn');
  }
  byId('quoteTile').onclick = next;
  byId('lunaCat').onclick = next;
  function fill() { byId('quoteList').value = list.join('\n'); byId('quoteStatus').textContent = (custom ? 'Your ' : 'Starter ') + list.length + ' quotes.'; }
  byId('settingsOpen').addEventListener('click', fill);
  byId('quoteSave').onclick = () => {
    const lines = clean(byId('quoteList').value.split('\n'));
    if (!lines.length) { list = defaults; custom = false; try { localStorage.removeItem(KEY); } catch {} }
    else { list = lines; custom = true; try { localStorage.setItem(KEY, JSON.stringify(list)); } catch {} }
    at = -1; fill(); next();
    byId('quoteStatus').textContent = 'Saved ' + list.length + ' quotes. A connected CoreS3 receives them now.';
    window.dispatchEvent(new Event('luna-quotes-change'));
  };
  window.LunaQuotes = { list: () => list.slice() };
  fill(); next();
})();
