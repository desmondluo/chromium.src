# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_SRC_FILES := $(call all-java-files-under, java/src)

# contentview and its dependencies
LOCAL_AIDL_INCLUDES := \
    $(LOCAL_PATH)/../content/public/android/java/src \
    $(LOCAL_PATH)/../net/android/java/src \
    $(LOCAL_PATH)/../third_party/eyesfree/src/android/java/src

LOCAL_SRC_FILES += \
    $(call all-java-files-under, ../content/public/android/java/src) \
    ../content/public/android/java/src/org/chromium/content/common/IChildProcessCallback.aidl \
    ../content/public/android/java/src/org/chromium/content/common/IChildProcessService.aidl \
    ../net/android/java/src/org/chromium/net/IRemoteAndroidKeyStoreCallbacks.aidl \
    ../net/android/java/src/org/chromium/net/IRemoteAndroidKeyStore.aidl \
    $(call all-java-files-under, ../base/android/java/src) \
    $(call all-java-files-under, ../media/base/android/java/src) \
    $(call all-java-files-under, ../net/android/java/src) \
    $(call all-java-files-under, ../ui/android/java/src) \
    $(call all-java-files-under, ../third_party/eyesfree/src/android/java/src) \
    $(call all-Iaidl-files-under, ../third_party/eyesfree/src/android/java/src)

# browser components
LOCAL_SRC_FILES += \
    $(call all-java-files-under, \
        ../components/web_contents_delegate_android/android/java/src) \
    $(call all-java-files-under, \
        ../components/navigation_interception/android/java/src) \

# This directory includes .java files that are generated by the normal gyp build, but are checked in
# for the Android build.
# TODO(torne, cjhopman): Consider removing this.
LOCAL_SRC_FILES += \
    $(call all-java-files-under, java/generated_src)

# Java files generated from .template rules. This list should match list of java dependencies in
# android_webview/android_webview.gyp
LOCAL_GENERATED_SOURCES := \
$(call intermediates-dir-for,GYP,shared)/templates/org/chromium/base/ApplicationState.java \
$(call intermediates-dir-for,GYP,shared)/templates/org/chromium/base/MemoryPressureLevelList.java \
$(call intermediates-dir-for,GYP,shared)/templates/org/chromium/content/browser/GestureEventType.java \
$(call intermediates-dir-for,GYP,shared)/templates/org/chromium/content/browser/SpeechRecognitionError.java \
$(call intermediates-dir-for,GYP,shared)/templates/org/chromium/content/browser/input/CanonicalAxisIndex.java \
$(call intermediates-dir-for,GYP,shared)/templates/org/chromium/content/browser/input/CanonicalButtonIndex.java \
$(call intermediates-dir-for,GYP,shared)/templates/org/chromium/content/browser/input/PopupItemType.java \
$(call intermediates-dir-for,GYP,shared)/templates/org/chromium/content/browser/input/SelectionEventType.java \
$(call intermediates-dir-for,GYP,shared)/templates/org/chromium/content/common/ResultCodes.java \
$(call intermediates-dir-for,GYP,shared)/templates/org/chromium/content/common/ScreenOrientationValues.java \
$(call intermediates-dir-for,GYP,shared)/templates/org/chromium/content_public/browser/PageTransitionTypes.java \
$(call intermediates-dir-for,GYP,shared)/templates/org/chromium/media/ImageFormat.java \
$(call intermediates-dir-for,GYP,shared)/templates/org/chromium/net/CertificateMimeType.java \
$(call intermediates-dir-for,GYP,shared)/templates/org/chromium/net/CertVerifyStatusAndroid.java \
$(call intermediates-dir-for,GYP,shared)/templates/org/chromium/net/NetError.java \
$(call intermediates-dir-for,GYP,shared)/templates/org/chromium/net/PrivateKeyType.java \
$(call intermediates-dir-for,GYP,shared)/templates/org/chromium/ui/WindowOpenDisposition.java \
$(call intermediates-dir-for,GYP,shared)/templates/org/chromium/ui/gfx/BitmapFormat.java \

# content dependencies on java components that are provided by the system on
# android
LOCAL_STATIC_JAVA_LIBRARIES += jsr305 guava

