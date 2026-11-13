# Layout validation

`accepted-changes.json` stays tracked because clean-clone layout gates consume it
to recognize reviewed code-layout changes. Its evidence paths refer to a private
validation archive; that archive contains the supporting build and measurement
records and is not part of this repository. Re-baselining is planned separately
after the hardware validation.
