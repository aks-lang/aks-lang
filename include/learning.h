// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#ifndef LEARNH
#define LEARNH

void LearnHelp(const char *topic);
void LearnTutorial(const char *action, const char *arg);
void LearnExamples(const char *action, const char *arg);
void LearnExamplesCopy(const char *name, const char *dest);
void LearnDoc(const char *inputpath, const char *outputpath);
void LearnPlayground(int port);
void LearnExplain(const char *err);
void LearnHighlight(const char *inputpath, const char *outputpath);
void LearnTour(const char *stepstr);
void LearnMigrate(const char *inputpath);
void LearnCheatsheet(const char *outputpath);

#endif // Learnh
