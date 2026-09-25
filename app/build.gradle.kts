import java.io.FileInputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Properties

plugins {
    id("com.android.application")
}

// Load keystore properties. Local test releases fall back to debug signing when
// a complete release keystore is unavailable.
val keystorePropertiesFile = rootProject.file("keystore.properties")
val keystoreProperties = Properties()
if (keystorePropertiesFile.exists()) {
    keystoreProperties.load(FileInputStream(keystorePropertiesFile))
}

val signingKeys = listOf("storeFile", "storePassword", "keyAlias", "keyPassword")
val missingSigningKeys = signingKeys.filter {
    keystoreProperties.getProperty(it)?.trim().isNullOrEmpty()
}
var hasReleaseSigning = keystorePropertiesFile.exists() && missingSigningKeys.isEmpty()

if (hasReleaseSigning) {
    val configuredStoreFile = file(keystoreProperties.getProperty("storeFile"))
    if (!configuredStoreFile.exists()) {
        logger.lifecycle(
            "[app] Release keystore file not found at $configuredStoreFile. " +
                "Falling back to debug signing for release."
        )
        hasReleaseSigning = false
    }
} else {
    logger.lifecycle(
        "[app] Release signing not fully configured ($missingSigningKeys). " +
            "Falling back to debug signing for release."
    )
}

android {
    namespace = "com.izzy2lost.psx2"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    signingConfigs {
        create("release") {
            if (hasReleaseSigning) {
                storeFile = file(keystoreProperties.getProperty("storeFile"))
                storePassword = keystoreProperties.getProperty("storePassword")
                keyAlias = keystoreProperties.getProperty("keyAlias")
                keyPassword = keystoreProperties.getProperty("keyPassword")
            }
        }
        // Cenit ships free and unofficial: the "debug" keystore is committed to
        // the repo so CI signs every build with the SAME key. Android then lets
        // a new version install over the previous one instead of failing with
        // INSTALL_FAILED_UPDATE_INCOMPATIBLE, which the ephemeral AGP-generated
        // debug key made impossible.
        getByName("debug") {
            storeFile = rootProject.file("debug.keystore")
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
    }

    defaultConfig {
        applicationId = "com.izzy2lost.psx2"
        minSdk = 26
        targetSdk = 36
        versionCode = 51
        versionName = "0.6.15"

        externalNativeBuild {
            cmake {
                // Avoid LTO core on Android to prevent x86 objects from leaking into arm64 link.
                arguments(
                    "-DANDROID=true",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DANDROID_STL=c++_static",
                    "-DPCSX2_PROFILER=OFF",
                    // Cenit 0.6.13 (revisión de ingeniería, prioridad 4): el CI
                    // necesita poder LEER la línea de compilación real de los
                    // archivos calientes (microVU, iR5900) para auditar si de
                    // verdad van con -O3/Release. Sin esta base no hay
                    // compile_commands.json y la auditoría es a ciegas.
                    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                    // Cenit 0.6.14: el núcleo no conocía su propia versionName, así
                    // que el HUD no podía distinguir dos Cenit consecutivos. Se
                    // hornea aquí y se lee en BuildVersion::AppVersion.
                    "-DCENIT_APP_VERSION=${defaultConfig.versionName}"
                )
            }
        }

        ndk {
            abiFilters.add("arm64-v8a")
        }
    }

    base {
        archivesName.set(
            "PSX2_${defaultConfig.versionCode}_${SimpleDateFormat("yyyyMMddHHmm").format(Date())}"
        )
    }

    buildTypes {
        release {
            // R8 primarily trims dependencies; app/JNI classes are preserved by proguard-rules.pro.
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            signingConfig = if (hasReleaseSigning) {
                signingConfigs.getByName("release")
            } else {
                signingConfigs.getByName("debug")
            }
            ndk {
                debugSymbolLevel = "SYMBOL_TABLE"
            }
        }

        debug {
            isMinifyEnabled = false
            isShrinkResources = false
            // Not debuggable: with FLAG_DEBUGGABLE set, ART refuses to AOT/JIT-compile
            // the app's Java code (input polling, UI updates) and keeps a debugger
            // watcher alive. The heavy lifting is native and already Release, but
            // this removes what little the framework throttles on the UI side, and
            // the CI debug keystore keeps installs upgradable either way.
            isDebuggable = false
        }
    }

    bundle {
        language {
            enableSplit = false
        }
        density {
            enableSplit = false
        }
        abi {
            enableSplit = true
        }
    }

    // Relax lint for local test release builds.
    lint {
        checkReleaseBuilds = false
        abortOnError = false
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
        isCoreLibraryDesugaringEnabled = true
    }

    externalNativeBuild {
        cmake {
            version = "3.22.1"
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
        resources {
            excludes.addAll(
                listOf(
                    "META-INF/DEPENDENCIES",
                    "META-INF/LICENSE",
                    "META-INF/NOTICE",
                    "META-INF/NOTICE.txt"
                )
            )
        }
    }

    buildToolsVersion = "36.1.0"
}

dependencies {
    coreLibraryDesugaring("com.android.tools:desugar_jdk_libs:2.0.4")
    implementation("androidx.appcompat:appcompat:1.7.1")
    implementation("com.google.android.material:material:1.14.0-alpha07")
    implementation("androidx.constraintlayout:constraintlayout:2.2.1")
    implementation("androidx.documentfile:documentfile:1.1.0")
    implementation("androidx.recyclerview:recyclerview:1.4.0")
    implementation("androidx.viewpager2:viewpager2:1.1.0")
    implementation("androidx.swiperefreshlayout:swiperefreshlayout:1.1.0")
    implementation("androidx.activity:activity:1.12.1")
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.work:work-runtime:2.11.2")
    implementation("com.github.bumptech.glide:glide:4.16.0")
    annotationProcessor("com.github.bumptech.glide:compiler:4.16.0")
}

val javaToolchainService = extensions.getByType(
    org.gradle.jvm.toolchain.JavaToolchainService::class.java
)

tasks.withType<JavaCompile>().configureEach {
    javaCompiler.set(
        javaToolchainService.compilerFor {
            languageVersion.set(JavaLanguageVersion.of(21))
        }
    )
}
