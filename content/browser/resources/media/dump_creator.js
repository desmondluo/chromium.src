// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


/**
 * Provides the UI to start and stop RTP recording, forwards the start/stop
 * commands to Chrome, and updates the UI based on dump updates. Also provides
 * creating a file containing all PeerConnection updates and stats.
 */
var DumpCreator = (function() {
  /**
   * @param {Element} containerElement The parent element of the dump creation
   *     UI.
   * @constructor
   */
  function DumpCreator(containerElement) {
    /**
     * True if the RTP packets are being recorded.
     * @type {bool}
     * @private
     */
    this.recording_ = false;

    /**
     * @type {!Object.<string>}
     * @private
     * @const
     */
    this.StatusStrings_ = {
      NOT_STARTED: 'not started.',
      RECORDING: 'recording...',
    },

    /**
     * The status of dump creation.
     * @type {string}
     * @private
     */
    this.status_ = this.StatusStrings_.NOT_STARTED;

    /**
     * The root element of the dump creation UI.
     * @type {Element}
     * @private
     */
    this.root_ = document.createElement('details');

    this.root_.className = 'peer-connection-dump-root';
    containerElement.appendChild(this.root_);
    var summary = document.createElement('summary');
    this.root_.appendChild(summary);
    summary.textContent = 'Create Dump';
    var content = document.createElement('div');
    this.root_.appendChild(content);

    content.innerHTML = '<button disabled></button> Status: <span></span>' +
        '<div><a><button>' +
        'Download the PeerConnection updates and stats data' +
        '</button></a></div>' +
        '<p><label><input type=checkbox>' +
        'Enable diagnostic audio recordings.</label></p>' +
        '<p>A diagnostic audio recording is used for analyzing audio' +
        ' problems. It contains the audio played out from the speaker and' +
        ' recorded from the microphone and is saved to the local disk.' +
        ' Checking this box will enable the recording for future WebRTC' +
        ' calls. When the box is unchecked or this page is closed, this' +
        ' recording functionality will be disabled.</p>';

    content.getElementsByTagName('button')[0].addEventListener(
        'click', this.onRtpToggled_.bind(this));
    content.getElementsByTagName('a')[0].addEventListener(
        'click', this.onDownloadData_.bind(this));
    content.getElementsByTagName('input')[0].addEventListener(
        'click', this.onAecRecordingChanged_.bind(this));

    this.updateDisplay_();
  }

  DumpCreator.prototype = {
    /** Mark the AEC recording checkbox checked.*/
    enableAecRecording: function() {
      this.root_.getElementsByTagName('input')[0].checked = true;
    },

    /**
     * Downloads the PeerConnection updates and stats data as a file.
     *
     * @private
     */
    onDownloadData_: function() {
      var textBlob =
          new Blob([JSON.stringify(peerConnectionDataStore, null, ' ')],
                                   {type: 'octet/stream'});
      var URL = window.URL.createObjectURL(textBlob);
      this.root_.getElementsByTagName('a')[0].href = URL;
      // The default action of the anchor will download the URL.
    },

    /**
     * Handles the event of toggling the rtp recording state.
     *
     * @private
     */
    onRtpToggled_: function() {
      if (this.recording_) {
        this.recording_ = false;
        this.status_ = this.StatusStrings_.NOT_STARTED;
        chrome.send('stopRtpRecording');
      } else {
        this.recording_ = true;
        this.status_ = this.StatusStrings_.RECORDING;
        chrome.send('startRtpRecording');
      }
      this.updateDisplay_();
    },

    /**
     * Handles the event of toggling the AEC recording state.
     *
     * @private
     */
    onAecRecordingChanged_: function() {
      var enabled = this.root_.getElementsByTagName('input')[0].checked;
      if (enabled) {
        chrome.send('enableAecRecording');
      } else {
        chrome.send('disableAecRecording');
      }
    },

    /**
     * Updates the UI based on the recording status.
     *
     * @private
     */
    updateDisplay_: function() {
      if (this.recording_) {
        this.root_.getElementsByTagName('button')[0].textContent =
            'Stop Recording RTP Packets';
      } else {
        this.root_.getElementsByTagName('button')[0].textContent =
            'Start Recording RTP Packets';
      }

      this.root_.getElementsByTagName('span')[0].textContent = this.status_;
    },

    /**
     * Set the status to the content of the update.
     * @param {!Object} update
     */
    onUpdate: function(update) {
      if (this.recording_) {
        this.status_ = JSON.stringify(update);
        this.updateDisplay_();
      }
    },
  };
  return DumpCreator;
})();
