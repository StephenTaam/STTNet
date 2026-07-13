
document.addEventListener('click', async (event) => {
  const toggle = event.target.closest('.nav-toggle');
  if (toggle) {
    const sidebar = document.querySelector('.sidebar');
    if (sidebar) {
      const open = sidebar.classList.toggle('open');
      toggle.setAttribute('aria-expanded', open ? 'true' : 'false');
    }
    return;
  }

  const button = event.target.closest('.copy');
  if (!button) return;
  const code = button.parentElement.querySelector('code');
  try {
    await navigator.clipboard.writeText(code.innerText);
    const old = button.textContent;
    button.textContent = button.dataset.done || 'Copied';
    setTimeout(() => button.textContent = old, 1200);
  } catch (_) {
    const selection = window.getSelection();
    const range = document.createRange();
    range.selectNodeContents(code);
    selection.removeAllRanges();
    selection.addRange(range);
    button.textContent = button.dataset.fail || 'Select and copy';
  }
});
