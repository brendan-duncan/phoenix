// Account plans, and the teams that can give their members one.
//
// Every account has a plan of its own (users.plan): beta and free are
// offered now; pro and enterprise are for later. What each allows is not
// decided yet, so `limits` is empty: a limit left out is the site's
// setting (the storage quota, say). Code that limits something asks
// planLimits(effectivePlan(...)) rather than the plan's name.
//
// A team (teams, team_members) is for a company organizing its people: its
// owner made it and adds members, it has a plan, and its groups
// (groups.team_id) hold its productions. A member gets the better of their
// own plan and the team's; a managed member (team_members.managed) has the
// team's plan only, and the team, not they, decides when the account goes.
// There is no way to make teams yet: the tables are here so that adding
// them later needs no reorganizing.

export const PLANS = {
  free: { label: 'Free', offered: true, limits: {} },
  // Free during the beta, perhaps with fewer limits than free.
  beta: { label: 'Beta', offered: true, limits: {} },
  pro: { label: 'Pro', offered: false, limits: {} },
  enterprise: { label: 'Enterprise', offered: false, limits: {} },
};

/** Plans from the most limited to the least. */
const RANK = ['free', 'beta', 'pro', 'enterprise'];

export function validPlan(plan) {
  return Object.hasOwn(PLANS, plan);
}

function better(a, b) {
  return RANK.indexOf(a) >= RANK.indexOf(b) ? a : b;
}

/**
 * The plan `user` has the use of, and where it comes from:
 * { plan, team: { id, name } | null, managed }.
 */
export function effectivePlan(store, user) {
  let plan = validPlan(user.plan) ? user.plan : 'free';
  let from = null;
  let managed = false;
  const teams = store.all(
    `SELECT t.id, t.name, t.plan, m.managed FROM team_members m JOIN teams t ON t.id = m.team_id
     WHERE m.user_id = ? ORDER BY m.managed DESC, t.created_at`, user.id);
  for (const team of teams) {
    if (!validPlan(team.plan)) {
      continue;
    }
    if (team.managed) {
      // The team decides: its plan, whatever the person's own.
      return { plan: team.plan, team: { id: team.id, name: team.name }, managed: true };
    }
    if (better(team.plan, plan) !== plan) {
      plan = team.plan;
      from = { id: team.id, name: team.name };
    }
  }
  return { plan, team: from, managed };
}

/** What `plan` allows (see the note above: nothing is decided yet). */
export function planLimits(plan) {
  return PLANS[plan]?.limits ?? {};
}

/** The team that manages `userId`'s account, if one does. */
export function managingTeam(store, userId) {
  return store.get(
    `SELECT t.* FROM team_members m JOIN teams t ON t.id = m.team_id
     WHERE m.user_id = ? AND m.managed = 1 ORDER BY t.created_at LIMIT 1`, userId) ?? null;
}
