# CI operator profile migration

`agent-skill-pipeline.json` contains portable project behavior. Machine labels,
workspace roots, credential references and publisher settings are selected by
the agent-skill library's strict external operator-profile contract.

Configure `KANO_JENKINS_OPERATOR_PROFILE` or trusted job argument
`operatorProfilePath` as an absolute path on the bootstrap agent, conventionally
`~/.kano/jenkins/operator-profile.json`. Select the exact
`kano-agent-backlog-skill` project entry. Set the initial bootstrap agent in
trusted job code; a file on that agent cannot choose the node that reads it.

The portable file requires this profile and cannot accept a build-parameter
path or a checkout fallback. Use a compatible canonical agent-skill library
with `AgentSkillOperatorProfile` support. Missing selection fails before work.
Preserve existing operator values privately, merge using the validated helper,
and compare the resolved configuration before activating a future pipeline.
Credential IDs are references; secret values remain in Jenkins credentials.
Do not publish the profile or emit its contents as a report.

KSDC-TSK-0013 verified exact offline round-trip equality for the extracted
profile. Live job configuration and execution are separate deployment gates;
this source change does not restart or reconfigure an ongoing job.
