(() => {
  'use strict';
  const key = 'vrkeyboard.home.shortcuts.v1';
  const defaults = [
    {name: 'Inara', url: 'https://inara.cz/elite/'},
    {name: 'Galmap', url: 'https://edastro.com/galmap/'},
    {name: 'EDSM', url: 'https://www.edsm.net/'}
  ];
  const $ = id => document.getElementById(id);
  let links = [], snapshot = null, editing = -1, baseline = null, available = true;
  function normalizeUrl(value) {
    value = value.trim();
    if (!value) throw new Error('Enter a web address.');
    if (!/^[a-z][a-z\d+.-]*:/i.test(value)) value = 'https://' + value;
    const url = new URL(value);
    if (!['http:', 'https:'].includes(url.protocol) || !url.hostname || url.username || url.password)
      throw new Error('Use an HTTP or HTTPS address without a username or password.');
    if (url.href.length > 2048) throw new Error('The web address is too long.');
    return url.href;
  }
  function validate(data) {
    if (!Array.isArray(data) || data.length > 50) throw new Error('Invalid shortcut list.');
    return data.map(item => {
      if (!item || typeof item.name !== 'string' || !item.name.trim() || item.name.length > 80 || typeof item.url !== 'string')
        throw new Error('Invalid shortcut.');
      return {name: item.name.trim(), url: normalizeUrl(item.url)};
    });
  }
  function load() {
    try {
      snapshot = localStorage.getItem(key);
      links = snapshot === null ? defaults.map(item => ({...item})) : validate(JSON.parse(snapshot));
      available = true;
      $('status').textContent = '';
    } catch (_) {
      available = false;
      $('status').textContent = 'Saved shortcuts could not be loaded. Your saved data has not been changed.';
    }
    render();
  }
  function render() {
    $('links').replaceChildren();
    links.forEach((item, index) => {
      const card = document.createElement('div'); card.className = 'shortcut';
      const link = document.createElement('a'); link.href = item.url; link.draggable = false;
      const label = document.createElement('span'); label.textContent = item.name; link.append(label);
      const edit = document.createElement('button'); edit.type = 'button'; edit.className = 'edit';
      edit.title = 'Edit ' + item.name; edit.setAttribute('aria-label', edit.title);
      // Static SVG only. User-supplied labels are always rendered with textContent.
      edit.innerHTML = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true"><path d="M4 16 16 4l4 4L8 20H4zm10-10 4 4"/></svg>';
      edit.disabled = !available; edit.addEventListener('click', () => open(index));
      card.append(link, edit); $('links').append(card);
    });
    const add = document.createElement('button'); add.id = 'add'; add.className = 'add'; add.type = 'button';
    add.textContent = '+'; add.title = 'Add shortcut'; add.setAttribute('aria-label', 'Add shortcut');
    add.disabled = !available || links.length >= 50; add.addEventListener('click', () => open(-1)); $('links').append(add);
    if (links.length >= 50) $('status').textContent = 'You can save up to 50 shortcuts.';
  }
  function open(index) {
    editing = index; baseline = snapshot;
    $('editor-title').textContent = index < 0 ? 'Add shortcut' : 'Edit shortcut';
    $('link-name').value = index < 0 ? '' : links[index].name;
    $('link-url').value = index < 0 ? '' : links[index].url;
    $('delete').hidden = index < 0;
    $('confirmation').hidden = true; $('actions').hidden = false; $('error').textContent = '';
    $('editor').showModal(); $('link-name').focus(); $('link-name').select();
  }
  function commit(next) {
    try {
      if (localStorage.getItem(key) !== baseline)
        throw new Error('Shortcuts changed in another tab. Cancel and reopen this editor.');
      const encoded = JSON.stringify(validate(next));
      localStorage.setItem(key, encoded);
      snapshot = encoded; links = next; $('editor').close(); $('status').textContent = ''; render();
    } catch (error) {
      $('error').textContent = error.message || 'Could not save shortcuts. Please try again.';
    }
  }
  $('link-form').addEventListener('submit', event => {
    event.preventDefault();
    if (!$('confirmation').hidden) return;
    const name = $('link-name').value.trim();
    if (!name) { $('error').textContent = 'Enter a name.'; $('link-name').focus(); return; }
    try {
      const item = {name, url: normalizeUrl($('link-url').value)};
      const next = links.map(link => ({...link}));
      if (editing < 0) next.push(item); else next[editing] = item;
      commit(next);
    } catch (error) { $('error').textContent = error.message; $('link-url').focus(); }
  });
  $('cancel').addEventListener('click', () => $('editor').close());
  $('editor').addEventListener('close', load);
  $('delete').addEventListener('click', () => { $('confirmation').hidden = false; $('actions').hidden = true; });
  $('keep').addEventListener('click', () => { $('confirmation').hidden = true; $('actions').hidden = false; });
  $('remove').addEventListener('click', () => commit(links.filter((_, index) => index !== editing)));
  document.addEventListener('dragstart', event => event.preventDefault());
  window.addEventListener('storage', event => {
    if (event.key !== key && event.key !== null) return;
    if ($('editor').open) $('error').textContent = 'Shortcuts changed in another tab. Cancel and reopen this editor.';
    else load();
  });
  load();
})();
