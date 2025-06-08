--- config.def.h.orig	2024-04-05 10:19:18 UTC
+++ config.def.h
@@ -118,10 +118,10 @@ 
 	[255] = 0,
 
 	/* more colors can be added after 255 to use with DefaultXX */
-	"#cccccc",
+	"#7aa2f7",
 	"#555555",
-	"gray90", /* default foreground colour */
-	"black", /* default background colour */
+	"#c0caf5", /* default foreground colour */
+	"#1a1b26", /* default background colour */
 };
 
 
@@ -141,7 +141,7 @@ static unsigned int defaultrcs = 257;
  * 6: Bar ("|")
  * 7: Snowman ("☃")
  */
-static unsigned int cursorshape = 2;
+static unsigned int cursorshape = 4;
 
 /*
  * Default columns and rows numbers
