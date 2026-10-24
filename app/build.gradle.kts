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
        applicationId = "com.huevoman77.cenit"
        minSdk = 26
        targetSdk = 36
        versionCode = 81
        // Cenit 0.6.44: la cola de cuadros deja de ser 0 por defecto en gama alta y
        // entra en "máxima jugabilidad" (ver native-lib.cpp y PlayabilityBundle).
        // El build "edge" compila solo para Cortex-A78 (LSE/dotprod): no puede
        // publicarse, en un arm64 viejo (A53) esas instrucciones son SIGILL.
        // Se activa con -PcenitEdge, nunca desde el CI.
        versionName = if (providers.gradleProperty("cenitEdge").isPresent) "0.6.44-edge" else "0.6.44"

        externalNativeBuild {
            cmake {
                arguments(
                    "-DANDROID=true",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DANDROID_STL=c++_static",
                    "-DPCSX2_PROFILER=OFF",
                    // Cenit 0.6.43: LTO del subconjunto que mas se beneficia (EE, VU,
                    // GS, SPU2, IPU). Estuvo apagado por un x86 que se colaba en el
                    // link arm64; la lista de fuentes ya esta separada por arquitectura
                    // (pcsx2/CMakeLists.txt, ARCH_X86 vs ARCH_ARM64), asi que ese motivo
                    // ya no aplica.
                    "-DLTO_PCSX2_CORE=ON",
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

                if (providers.gradleProperty("cenitEdge").isPresent) {
                    arguments("-DCENIT_EDGE_CPU=ON")
                }
            }
        }

        ndk {
            abiFilters.add("arm64-v8a")
        }
    }

    base {
        archivesName.set(
            "Cenit_${defaultConfig.versionCode}_${SimpleDateFormat("yyyyMMddHHmm").format(Date())}"
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
            // Cenit 0.6.43: -PcenitProfile deja los simbolos dentro del APK para que
            // simpleperf pueda nombrar funciones al perfilar en el telefono. Solo local.
            if (providers.gradleProperty("cenitProfile").isPresent) {
                keepDebugSymbols.add("**/libemucore.so")
            }
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
