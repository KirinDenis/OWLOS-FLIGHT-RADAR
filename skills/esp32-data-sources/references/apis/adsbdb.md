# adsbdb: routes, tried in a contribution and declined

An aircraft and route database with an API. A contribution to OWLOS Flight Radar (its PR #3) took each plane's route from it.

- Checked on one real sky: about 1 route in 5 did not fit the plane, 18 of 93. [measured]
- Its README restricts the use of its route data.
- The pull request was closed for those two reasons. The firmware shows routes only from FR24.
- For another try at routes without FR24 see `vrs-standing-data.md`: CC0, with a plausibility check against the plane's position
  and heading.
