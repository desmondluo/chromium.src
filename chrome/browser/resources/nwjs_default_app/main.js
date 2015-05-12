chrome.app.runtime.onLaunched.addListener(function() {
  chrome.app.window.create(
      'nw_blank.html',
      {'id': 'nwjs_default_app', 'height': 550, 'width': 750});
});
