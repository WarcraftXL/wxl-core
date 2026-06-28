# wxl-modern-anim

External `.anim` files for M2 models are loaded after the main `.m2` parse, so they do not pass through
`wxl-modern-m2`'s model-load transform. This module gives those sibling animation files their own narrow
path.

It normalizes only the external-animation surfaces the 3.3.5 client cannot consume directly:

- host: claims archive names ending in `.anim` when the bytes need rewriting and serves a normalized copy
- runtime: hooks the native external-animation read completion as a fallback for natively served files
- shared transform: strips `AFM2`/`AFSB` chunk wrappers down to their raw track payload and, for rare
  MD20-like animation headers, rewrites the inner version to the client version and fixes sequence metadata

It intentionally does not run the full M2 downport on `.anim` data.
