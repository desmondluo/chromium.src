// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_SESSION_USER_SESSION_MANAGER_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_SESSION_USER_SESSION_MANAGER_H_

#include <string>

#include "base/basictypes.h"
#include "base/memory/singleton.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chromeos/base/locale_util.h"
#include "chrome/browser/chromeos/login/auth/authenticator.h"
#include "chrome/browser/chromeos/login/signin/oauth2_login_manager.h"
#include "chrome/browser/chromeos/login/users/user.h"
#include "chromeos/login/auth/user_context.h"
#include "net/base/network_change_notifier.h"

class PrefRegistrySimple;
class PrefService;
class Profile;

namespace chromeos {

// UserSessionManager is responsible for starting user session which includes:
// load and initialize Profile (including custom Profile preferences),
// mark user as logged in and notify observers,
// initialize OAuth2 authentication session,
// initialize and launch user session based on the user type.
class UserSessionManager :
    public OAuth2LoginManager::Observer,
    public net::NetworkChangeNotifier::ConnectionTypeObserver,
    public base::SupportsWeakPtr<UserSessionManager> {
 public:
  class Delegate {
   public:
    // Called after profile is loaded and prepared for the session.
    virtual void OnProfilePrepared(Profile* profile) = 0;

#if defined(ENABLE_RLZ)
    // Called after post-profile RLZ initialization.
    virtual void OnRlzInitialized() {}
#endif
   protected:
    virtual ~Delegate() {}
  };

  // Returns UserSessionManager instance.
  static UserSessionManager* GetInstance();

  // Called when user is logged in to override base::DIR_HOME path.
  static void OverrideHomedir();

  // Registers session related preferences.
  static void RegisterPrefs(PrefRegistrySimple* registry);

  // Start user session given |user_context| and |authenticator| which holds
  // authentication context (profile).
  void StartSession(const UserContext& user_context,
                    scoped_refptr<Authenticator> authenticator,
                    bool has_auth_cookies,
                    bool has_active_session,
                    Delegate* delegate);

  // Perform additional actions once system wide notification
  // "UserLoggedIn" has been sent.
  void PerformPostUserLoggedInActions();

  // Restores authentication session after crash.
  void RestoreAuthenticationSession(Profile* profile);

  // Initialize RLZ.
  void InitRlz(Profile* profile);

  // Returns true when the browser has crashed and restarted during the current
  // user's session.
  bool HasBrowserRestarted() const;

  // TODO(nkostylev): Drop these methods once LoginUtilsImpl::AttemptRestart()
  // is migrated.
  OAuth2LoginManager::SessionRestoreStrategy GetSigninSessionRestoreStrategy();
  bool exit_after_session_restore() { return exit_after_session_restore_; }
  void set_exit_after_session_restore(bool value) {
    exit_after_session_restore_ = value;
  }

  // Invoked when the user is logging in for the first time, or is logging in as
  // a guest user.
  static void SetFirstLoginPrefs(PrefService* prefs);

  // Gets/sets Chrome OAuth client id and secret for kiosk app mode. The default
  // values can be overridden with kiosk auth file.
  bool GetAppModeChromeClientOAuthInfo(
      std::string* chrome_client_id,
      std::string* chrome_client_secret);
  void SetAppModeChromeClientOAuthInfo(
      const std::string& chrome_client_id,
      const std::string& chrome_client_secret);

  // Changes browser locale (selects best suitable locale from different
  // user settings). Returns true if callback will be called.
  // Returns true if callback will be called.
  bool RespectLocalePreference(
      Profile* profile,
      const User* user,
      scoped_ptr<locale_util::SwitchLanguageCallback> callback) const;

 private:
  friend struct DefaultSingletonTraits<UserSessionManager>;

  typedef std::set<std::string> SessionRestoreStateSet;

  UserSessionManager();
  virtual ~UserSessionManager();

  // OAuth2LoginManager::Observer overrides:
  virtual void OnSessionRestoreStateChanged(
      Profile* user_profile,
      OAuth2LoginManager::SessionRestoreState state) OVERRIDE;
  virtual void OnNewRefreshTokenAvaiable(Profile* user_profile) OVERRIDE;

  // net::NetworkChangeNotifier::ConnectionTypeObserver overrides:
  virtual void OnConnectionTypeChanged(
      net::NetworkChangeNotifier::ConnectionType type) OVERRIDE;

  void CreateUserSession(const UserContext& user_context,
                         bool has_auth_cookies);
  void PreStartSession();
  void StartCrosSession();
  void NotifyUserLoggedIn();
  void PrepareProfile();

  // Callback for asynchronous profile creation.
  void OnProfileCreated(const std::string& user_id,
                        bool is_incognito_profile,
                        Profile* profile,
                        Profile::CreateStatus status);

  // Callback for Profile::CREATE_STATUS_CREATED profile state.
  // Initializes basic preferences for newly created profile. Any other
  // early profile initialization that needs to happen before
  // ProfileManager::DoFinalInit() gets called is done here.
  void InitProfilePreferences(Profile* profile,
                              const std::string& email);

  // Callback for Profile::CREATE_STATUS_INITIALIZED profile state.
  // Profile is created, extensions and promo resources are initialized.
  void UserProfileInitialized(Profile* profile, bool is_incognito_profile);

  // Callback to resume profile creation after transferring auth data from
  // the authentication profile.
  void CompleteProfileCreateAfterAuthTransfer(Profile* profile);

  // Finalized profile preparation.
  void FinalizePrepareProfile(Profile* profile);

  // Initializes member variables needed for session restore process via
  // OAuthLoginManager.
  void InitSessionRestoreStrategy();

  // Restores GAIA auth cookies for the created user profile from OAuth2 token.
  void RestoreAuthSessionImpl(Profile* profile,
                              bool restore_from_auth_cookies);

  // Initializes RLZ. If |disabled| is true, RLZ pings are disabled.
  void InitRlzImpl(Profile* profile, bool disabled);

  // Get the NSS cert database for the primary user and start certificate
  // loader with it.
  void InitializeCertsForPrimaryUser(Profile* profile);

  Delegate* delegate_;

  // Authentication/user context.
  UserContext user_context_;
  scoped_refptr<Authenticator> authenticator_;

  // True if the authentication context's cookie jar contains authentication
  // cookies from the authentication extension login flow.
  bool has_auth_cookies_;

  // OAuth2 session related members.

  // True if we should restart chrome right after session restore.
  bool exit_after_session_restore_;

  // Sesion restore strategy.
  OAuth2LoginManager::SessionRestoreStrategy session_restore_strategy_;

  // OAuth2 refresh token for session restore.
  std::string oauth2_refresh_token_;

  // Set of user_id for those users that we should restore authentication
  // session when notified about online state change.
  SessionRestoreStateSet pending_restore_sessions_;

  // Kiosk mode related members.
  // Chrome oauth client id and secret - override values for kiosk mode.
  std::string chrome_client_id_;
  std::string chrome_client_secret_;

  DISALLOW_COPY_AND_ASSIGN(UserSessionManager);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_SESSION_USER_SESSION_MANAGER_H_
