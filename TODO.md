# Experiments

## Benign attribute changes having big impacts?

- Run a baseline test on a fixed seed
- Re-run but with a changed attribute on an unused substitute

If the entire result is different then tiny changes impact randomness, making
runs on the same seed less trustworthy.

- I can finally learn whether height makes a difference in game or whether it only influences the ceiling of your jumping reach (I've heard conflicting anecdotal evidence on this)
- Put a goalkeeper in a middling team with a non-overpowered tactic and test _every_ attribute on him to see what happens to his xGA vs goals conceded. (Testing every combination of every attribute isn't possible because of the eventual heat death of the universe but I've estimated it would take one machine ~26 hours to test every attribute 1-20).
- Test whether there's a rock-paper-scissors effect with tactics or whether some are universally strong.
- Test how powerful match plans are (right now I can't find match plan settings in the exported fixture blob so it's unclear to me how these instructions are fed in to the match engine).
- Test assistant manager attributes on his ability to manage a match.
- Best tactics/tweaks for underdogs, mid-table and strong teams etc.
- Does CA/PA affect a player's on-pitch performance independent of his attributes?
- How much does positional familiarity affect on-pitch performance?
- What's the best corner/free-kick tactic?
- Does footedness matter?
  - If single-footed players can both play on the same wing, does one do better than the other?
  - What's the penalty for playing players on their non-preferred CM/CB side? What's the bonus for playing them in the preferred one?
- Find where player bonds are stored in the fixture data and look at the impact on results for maximising these.
- What's the performance penalty for players who have low tactical familiarity?
  - Test all different aspects of tactical familiarity
- What is the performance impact of low(er) morale, sharpness, fatigue, and condition?
- what attributes matter and to what extent (a pipe dream but I live in hope)
- what impact does Match Focus (e.g. "Focus on Attacking Corners for the upcoming match";) actually have?
- Does venue matter?
- Does competition importance matter?
- Do relative team reputations matter?
- Does weather matter?
- Do referees have attributes or are they just a name and a face on top of a random number generator?
  - Also, the game generates a random number for referees before you advance to a match; what impact does this have? Do they catch more or less based on it? Are they more liberal with cards/penalties?
- Does squad harmony/manager support factor in to results or is this more like a baseline for players' happiness that they return to when no other factors are at play?
- What does training happiness actually mean?

# Features

- Swap in players
- Heal all
- Fix morale
- Edit players/training/weather/venue in the app
