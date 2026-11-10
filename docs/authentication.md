# Connect a Google account

1. Create a Google Cloud project and enable **Google Calendar API**.
2. In **Google Auth Platform → Branding**, choose a name identifying your app.
   Avoid Google product names in the consent-screen name.
3. Under **Audience**, choose **External** for a personal Google account. While
   testing, add the account you want to use under **Test users**.
4. Under **Data Access**, add the scopes the default `OAuthConfig` requests:

   ```text
   https://www.googleapis.com/auth/calendar.calendarlist.readonly
   https://www.googleapis.com/auth/calendar.events
   ```

5. Under **Clients**, create a **Desktop app** OAuth client. Download its JSON
   configuration and keep it outside Git.

Run `calendar-list` from your build directory with the JSON path. Open the
printed authorization URL on the same machine. The loopback callback completes
sign-in and the example prints the available calendars. The example does not
persist credentials or make event changes.

In an application, pass the client configuration to `OAuthSession`, supply a
browser opener, and keep the refresh token in an OS credential store. Restore
it with `restore_tokens()` at startup. The library handles refreshing access
tokens; persist a changed refresh token when necessary.

Testing-mode Calendar refresh tokens expire after seven days. For longer-lived
personal use, review Google's publishing rules before changing status.

- [Desktop OAuth setup](https://developers.google.com/identity/protocols/oauth2/native-app)
- [Consent screen and scopes](https://developers.google.com/workspace/guides/configure-oauth-consent)
- [Audience and testing limits](https://support.google.com/cloud/answer/15549945)
- [Secure token handling](https://developers.google.com/identity/protocols/oauth2/resources/best-practices)
