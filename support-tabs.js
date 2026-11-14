const links = document.querySelectorAll('.support-tabs a');
function selectSupportTab() {
  const projects = location.hash === '#projets';
  document.getElementById('soutien').hidden = projects;
  document.getElementById('projets').hidden = !projects;
  links.forEach(link => {
    if (link.hash === (projects ? '#projets' : '#soutien')) {
      link.setAttribute('aria-current', 'true');
    } else {
      link.removeAttribute('aria-current');
    }
  });
}
window.addEventListener('hashchange', selectSupportTab);
selectSupportTab();

document.querySelectorAll('[data-wallet-copy]').forEach(button => {
  const label = button.textContent;
  let feedbackTimer;
  button.addEventListener('click', async () => {
    try {
      await navigator.clipboard.writeText(button.dataset.walletCopy);
      button.textContent = '✓ ' + label;
      clearTimeout(feedbackTimer);
      feedbackTimer = setTimeout(() => { button.textContent = label; }, 1800);
    } catch {
      const selection = window.getSelection();
      if (selection) selection.selectAllChildren(button.previousElementSibling);
    }
  });
});
