// HellwalkerRL website configuration (web/README.md walks through every value).
//
// Leave apiKey / projectId empty and the site runs in DEMO mode: generated sample players and fights, with a banner.
// Add ?mock=http://127.0.0.1:8099 to the address to use the local mock (web/dev/mock_firebase.py) instead.
// The Firebase web API key is not a secret - the security is web/firebase/firestore.rules - but restrict it to the
// Identity Toolkit API, the Token Service API and the Cloud Firestore API in the Google Cloud console (README step 6).
export default {
	apiKey: "",     // Firebase web API key (Project settings -> General -> Web API key)
	projectId: "",  // Firebase project id, e.g. "hellwalker-research"
	itchUrl: "",    // the game's itch.io page, e.g. "https://yourname.itch.io/hellwalker"
	siteUrl: "",    // where this site is hosted, e.g. "https://hellwalker.onrender.com" (the game opens {siteUrl}/#/me?t=...)
	contact: "",    // research contact shown on #/research (an email address or a URL)
};
