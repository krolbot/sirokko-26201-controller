// SYNTHETIC BROWSER ROUTE FIXTURE ONLY. It emulates the HTTP contract for
// Playwright; production code never imports this file or manufactures state.
import { syntheticState } from './synthetic-state.js';

const json = (route, status, body) => route.fulfill({
  status,
  contentType: 'application/json; charset=utf-8',
  body: JSON.stringify(body),
});

export class SyntheticRouteApi {
  constructor(state = syntheticState()) {
    this.state = state;
    this.signedIn = false;
    this.offline = false;
    this.challengeVersion = null;
    this.commandResponse = { status: 'QUEUED' };
    this.otaResponse = { status: 'VERIFIED_ACTIVATION_QUEUED', expected_version: 'test-1.1.0' };
    this.commands = [];
    this.stops = [];
    this.challenges = [];
    this.otaHeaders = [];
    this.onCommand = null;
    this.deferChallenge = false;
    this._resolveChallenge = null;
  }

  releaseChallenge() {
    this._resolveChallenge?.();
    this._resolveChallenge = null;
  }

  async handle(route) {
    const request = route.request();
    const path = new URL(request.url()).pathname;
    if (this.offline) return route.abort('failed');
    if (path === '/api/login') {
      this.signedIn = true;
      return json(route, 200, { status: 'OK' });
    }
    if (!this.signedIn) return json(route, 401, { status: 'REJECTED', code: 'AUTH' });
    if (path === '/api/state') return json(route, 200, this.state);
    if (path === '/api/challenge') {
      const payload = request.postDataJSON();
      this.challenges.push(payload);
      if (this.deferChallenge) await new Promise((resolve) => { this._resolveChallenge = resolve; });
      return json(route, 200, { status: 'CHALLENGE', token: 'feed', state_version: this.challengeVersion ?? this.state.state_version, expires_ms: 'ffff', boot_id: this.state.boot_id });
    }
    if (path === '/api/command') {
      const payload = request.postDataJSON();
      if (payload.action === 'STOP') {
        this.stops.push(payload);
        return json(route, 200, { status: 'STOP_REQUESTED' });
      }
      this.commands.push(payload);
      const response = this.onCommand ? this.onCommand(payload) : this.commandResponse;
      return json(route, 200, response);
    }
    if (path === '/api/ota') {
      const headers = await request.allHeaders();
      this.otaHeaders.push({
        token: headers['x-sirokko-token'],
        stateVersion: headers['x-sirokko-state-version'],
        commandId: headers['x-sirokko-command-id'],
        sha256: headers['x-sirokko-upload-sha256'],
      });
      return json(route, 200, this.otaResponse);
    }
    return json(route, 404, { status: 'REJECTED', code: 'NOT_FOUND' });
  }
}
