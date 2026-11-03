Updated RPCs
------------

- The `sendrawtransaction` RPC has a new `delay_after_lock_time` argument that
  schedules a private broadcast for a later block height: the transaction is
  broadcast once the chain reaches the height `nLockTime + delay_after_lock_time`
  (or immediately, if that height has already been reached). Only height-based
  lock times are supported. `delay_after_lock_time` requires `-privatebroadcast`
  and is rejected otherwise. A scheduled transaction is validated when it is
  submitted; if it cannot be mined in the next block yet (e.g. its `nLockTime`
  is in the future), it is still accepted, but some checks (e.g. inputs and fee)
  are only done when it is due. It is fully validated again when it is due, and
  is dropped if it is not valid then. For a scheduled transaction, a successful result means that it is
  scheduled, not that it has been sent. Scheduled transactions are shown by
  `getprivatebroadcastinfo` (with their `release_height`) and can be cancelled
  with `abortprivatebroadcast`. They are not saved to disk and are lost on
  restart.
