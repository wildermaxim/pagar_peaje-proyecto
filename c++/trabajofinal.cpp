// ============================================================
//  PEAJE PAMPLONITA — Servidor HTTP en C++
//  Escucha en el puerto 8080 y responde peticiones del navegador.
//  Usa SQLite como base de datos local (database.db).
//  Usa Winsock2 para manejar conexiones de red en Windows.
// ============================================================

// --- Librerías estándar de C++ ---
#include <iostream>   // cout: imprimir mensajes en consola
#include <string>     // tipo string y sus métodos
#include <sstream>    // stringstream: leer texto como si fuera un archivo
#include <fstream>    // ifstream: leer archivos del disco (HTML, CSS)
#include <ctime>      // time(), localtime_s(): obtener hora y fecha del sistema
#include <regex>      // regex, regex_match(): validar formato de placas
#include <algorithm>  // transform(): convertir texto a mayúsculas

// --- Librerías de Windows ---
#include <winsock2.h> // SOCKET, bind(), listen(), accept(): red en Windows
#include <windows.h>  // WSADATA, WSAStartup(): inicializar Winsock

// --- Base de datos ---
#include "sqlite3.h"  // SQLite embebido: toda la BD corre dentro del mismo .exe

// Enlaza automáticamente la librería ws2_32.lib al compilar (red de Windows)
#pragma comment(lib, "ws2_32.lib")

// Usamos el espacio de nombres estándar para no escribir "std::" todo el tiempo
using namespace std;

// Puntero global a la base de datos SQLite.
// Es global porque lo usan todas las funciones del router sin necesidad de pasarlo.
sqlite3* db;


// ============================================================
//  SECCIÓN: UTILIDADES
//  Funciones pequeñas de apoyo usadas por todo el programa.
// ============================================================

// Devuelve la hora actual del sistema en formato "HH:MM:SS"
string hora() {
    time_t t = time(0);          // obtiene el tiempo actual como número (segundos desde 1970)
    tm tmPtr;                    // estructura que guarda hora, minuto, segundo, etc.
    localtime_s(&tmPtr, &t);     // convierte el número a hora local (versión segura de Windows)
    char buf[10];                // buffer donde se escribirá el texto formateado
    strftime(buf, 10, "%H:%M:%S", &tmPtr); // da formato "14:35:07" al buffer
    return buf;                  // retorna el buffer como string
}

// Devuelve la fecha actual del sistema en formato "YYYY-MM-DD"
string fecha() {
    time_t t = time(0);          // tiempo actual en segundos
    tm tmPtr;                    // estructura de fecha/hora
    localtime_s(&tmPtr, &t);     // convierte a hora local
    char buf[15];                // buffer para el texto
    strftime(buf, 15, "%Y-%m-%d", &tmPtr); // da formato "2025-06-15"
    return buf;
}

// Retorna true si la hora actual está entre las 6:00 AM y las 8:59 AM (hora pico)
bool esHoraPico() {
    time_t t = time(0);          // tiempo actual
    tm tmPtr;                    // estructura de fecha/hora
    localtime_s(&tmPtr, &t);     // convierte a hora local
    // tm_hour es el campo de la hora (0-23); 6 = 6AM, 9 = 9AM (no incluida)
    return (tmPtr.tm_hour >= 8 && tmPtr.tm_hour < 12);
}

// Valida que una placa tenga formato colombiano (ABC123) o venezolano (AB123CD)
bool placaValida(const string& placa) {
    regex col("^[A-Z]{3}[0-9]{3}$");         // 3 letras + 3 números (Colombia)
    regex ven("^[A-Z]{2}[0-9]{3}[A-Z]{2}$"); // 2 letras + 3 números + 2 letras (Venezuela)
    return regex_match(placa, col) || regex_match(placa, ven); // valida si cumple alguno
}

// Extrae el valor de un parámetro de un query string o formulario POST.
// Ejemplo: en "placa=ABC123&peso=5", getQueryParam(data, "placa") devuelve "ABC123"
string getQueryParam(const string& data, const string& key) {
    size_t pos = data.find(key + "=");      // busca "placa=" en el texto
    if (pos == string::npos) return "";     // si no lo encuentra, retorna vacío
    pos += key.length() + 1;               // avanza el cursor hasta el valor (después del "=")
    size_t end = data.find("&", pos);      // busca el "&" que termina este parámetro
    return data.substr(pos, end - pos);    // extrae solo el valor entre "=" y "&"
    // Nota: si end == npos (último param), substr con npos extrae hasta el final, lo cual es correcto
}


// ============================================================
//  SECCIÓN: HTTP
//  Construye respuestas HTTP válidas para enviar al navegador.
// ============================================================

// Envuelve un cuerpo de respuesta en cabeceras HTTP 200 OK.
// - body: el contenido a enviar (HTML, JSON, CSS...)
// - type: el Content-Type (por defecto "application/json")
// Access-Control-Allow-Origin: * permite que el navegador acepte la respuesta
// aunque el servidor esté en localhost (evita errores CORS)
string http(string body, string type = "application/json") {
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: " + type + "; charset=utf-8\r\n"
           "Access-Control-Allow-Origin: *\r\n\r\n" // linea en blanco obligatoria antes del cuerpo
           + body;
}


// ============================================================
//  SECCIÓN: ARCHIVOS
//  Lee archivos del disco para servirlos como respuesta HTTP.
// ============================================================

// Lee un archivo de texto completo y retorna su contenido como string.
// Se usa para servir p.html y css.css desde el disco.
string readFile(const string& path) {
    ifstream file(path);                          // abre el archivo en modo lectura
    if (!file.is_open()) {                        // si no se pudo abrir (ruta incorrecta, etc.)
        return "<h1>Error cargando archivo</h1>"; // retorna un mensaje de error en HTML
    }
    stringstream ss;       // buffer en memoria
    ss << file.rdbuf();    // vuelca todo el contenido del archivo al buffer
    return ss.str();       // retorna el buffer como string
}


// ============================================================
//  SECCIÓN: BASE DE DATOS
//  Crea las tablas si no existen al arrancar el servidor.
// ============================================================

// Inicializa la base de datos SQLite y crea las tablas necesarias.
// Se llama una sola vez al inicio del programa (en main).
void initDB() {
    // Abre (o crea si no existe) el archivo database.db en la carpeta del ejecutable
    if (sqlite3_open("database.db", &db)) {
        cout << "Error DB\n"; // muestra error en consola si no se puede abrir
    }

    // Tabla "usuarios": vehículos registrados con TAG
    // - placa: identificador único del vehículo (clave primaria)
    // - prop:  nombre del propietario
    // - cat:   categoría del vehículo (I, II, III, IV, V, IE)
    // - peso:  peso registrado en toneladas
    // - saldo: saldo disponible en el TAG (en pesos colombianos)
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS usuarios("
        "placa TEXT PRIMARY KEY,"
        "prop TEXT, cat TEXT, peso REAL, saldo REAL);",
        0, 0, 0); // sin callback, sin argumento extra, sin captura de error SQL

    // Tabla "historial": cada cobro realizado queda registrado aqui
    // - id:        número de recibo autoincremental (PRIMARY KEY)
    // - placa, categoria, peso: datos del vehículo en el momento del cobro
    // - tarifa, recargo, multa, total: desglose del cobro en pesos
    // - metodo:    "TAG" o "EFECTIVO"
    // - hora, fecha: momento exacto en que ocurrió el cobro
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS historial("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "placa TEXT, categoria TEXT, peso REAL,"
        "tarifa REAL, recargo REAL, multa REAL, total REAL,"
        "metodo TEXT, hora TEXT, fecha TEXT);",
        0, 0, 0);
}


// ============================================================
//  SECCIÓN: TARIFAS Y CÁLCULOS
//  Toda la lógica de precios vive aquí, en el servidor.
//  El JS solo recibe los resultados ya calculados; no los calcula él.
// ============================================================

// Retorna la tarifa base en pesos colombianos según la categoría del vehículo.
// Estas tarifas son las oficiales del Peaje Pamplonita.
double getTarifa(string cat) {
    if (cat == "I")  return 9500;
    if (cat == "II") return 13300;
    if (cat == "III")return 13300;
    if (cat == "IV") return 13300;
    if (cat == "V")  return 29700;

    // 🆕 nuevas categorías
    if (cat == "VI")  return 38400;
    if (cat == "VII") return 43700;

    // IE original
    if (cat == "IE") return 2400;

    return 10000;
}

// Retorna el peso máximo permitido en toneladas para cada categoría.
// Si el vehículo supera este límite en más de 1 tonelada, se aplica multa.
// Estos valores son exactamente los mismos que se envían al JS en /api/categorias,
// garantizando consistencia entre lo que ve el operador y lo que calcula el servidor.
double getPesoMax(string cat) {
    if (cat == "I")  return 5.0;
    if (cat == "II") return 17.0;
    if (cat == "III")return 10.1;
    if (cat == "IV") return 17.0;
    if (cat == "V")  return 28.0;

    // 🆕 nuevas categorías
    if (cat == "VI")  return 35.0;
    if (cat == "VII") return 40.0;

    // IE original
    if (cat == "IE") return 5.0;

    return 5.0;
}

// Calcula la multa por exceso de peso.
// Regla: si el peso declarado supera en MAS de 1 tonelada el límite de su categoría,
// se cobra $1.266.222 y el vehículo debe ser detenido por las autoridades.
// Ejemplo: Cat. I (límite 5T) con 6.2T → exceso de 1.2T → SI hay multa.
// Ejemplo: Cat. I (límite 5T) con 5.8T → exceso de 0.8T → NO hay multa (tolerancia de 1T).
double getMulta(string cat, double peso) {
    double pesoMax = getPesoMax(cat);          // obtiene el límite permitido para esta categoría
    if (peso > pesoMax + 1.0) return 1266222;  // supera el límite + 1T de tolerancia → multa
    return 0;                                  // dentro del rango permitido → sin multa
}


// ============================================================
//  SECCIÓN: ROUTER (corazón del servidor)
//  Recibe la petición HTTP cruda, identifica qué ruta es,
//  ejecuta la lógica correspondiente y retorna la respuesta.
// ============================================================

string router(string req) {

    // Extrae el método (GET/POST) y la ruta (/api/...) de la primera línea HTTP.
    // Ejemplo de primera línea: "GET /api/buscar-placa?placa=ABC123 HTTP/1.1"
    string method, path;
    stringstream ss(req); // convierte la petición en un stream legible línea a línea
    ss >> method >> path; // lee las dos primeras palabras: método y ruta

    // ── RUTA: GET / ─────────────────────────────────────────────────────────────
    // Sirve la interfaz principal cuando el navegador abre http://localhost:8080
    if (method == "GET" && path == "/")
        return http(readFile("html/p.html"), "text/html"); // lee p.html del disco y lo envía

    // ── RUTA: GET /css/css.css ───────────────────────────────────────────────────
    // Sirve la hoja de estilos cuando el navegador la solicita al cargar el HTML
    if (method == "GET" && path == "/css/css.css")
        return http(readFile("css/css.css"), "text/css"); // lee css.css y lo envía

    // ── RUTA: GET /api/categorias ────────────────────────────────────────────────
    // Retorna la lista de categorías con descripción, peso máximo y tarifa.
    // El JS usa esta respuesta para llenar la tabla de tarifas y el select del formulario.
 if (method == "GET" && path == "/api/categorias") {
    return http(
        "{"
        "\"normales\":["
        "{\"nombre\":\"I\",\"descripcion\":\"2 ejes\",\"pesoMaxTon\":5,\"tarifa\":9500},"
        "{\"nombre\":\"II\",\"descripcion\":\"Bus\",\"pesoMaxTon\":17,\"tarifa\":13300},"
        "{\"nombre\":\"III\",\"descripcion\":\"Camion\",\"pesoMaxTon\":10.1,\"tarifa\":13300},"
        "{\"nombre\":\"IV\",\"descripcion\":\"Camion grande\",\"pesoMaxTon\":17,\"tarifa\":13300},"
        "{\"nombre\":\"V\",\"descripcion\":\"Camiones de tres y cuatro ejes\",\"pesoMaxTon\":28,\"tarifa\":29700},"
        "{\"nombre\":\"VI\",\"descripcion\":\"Camiones de cinco ejes\",\"pesoMaxTon\":35,\"tarifa\":38400},"
        "{\"nombre\":\"VII\",\"descripcion\":\"Camiones de seis ejes o mas\",\"pesoMaxTon\":40,\"tarifa\":43700}"
        "],"
        "\"ie\":["
        "{\"nombre\":\"IE\",\"descripcion\":\"Residente\",\"pesoMaxTon\":5,\"tarifa\":2400}"
        "]"
        "}"
    );
}

    // ── RUTA: GET /api/buscar-placa?placa=XXX ────────────────────────────────────
    // Busca un vehículo en la tabla "usuarios" (vehículos con TAG registrados).
    // Si lo encuentra, calcula tarifa y recargo y los incluye en la respuesta.
    // El JS usa esta respuesta para decidir si muestra el panel TAG o el panel sin TAG.
    if (method == "GET" && path.find("/api/buscar-placa") != string::npos) {

        // Extrae el valor de "placa" del query string de la URL (?placa=ABC123)
        string placa = getQueryParam(path, "placa");
        // Convierte a mayúsculas para que "abc123" funcione igual que "ABC123"
        transform(placa.begin(), placa.end(), placa.begin(), ::toupper);

        // Valida el formato antes de hacer cualquier consulta a la BD
        if (!placaValida(placa)) {
            return http("{\"error\":\"Formato de placa invalido\"}");
        }

        // Prepara una consulta parametrizada: el "?" se sustituirá de forma segura
        // (esto previene inyección SQL, donde un atacante podría meter código en la placa)
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db,
            "SELECT placa, prop, cat, peso, saldo FROM usuarios WHERE placa=?",
            -1, &stmt, nullptr);

        // Sustituye el "?" con el valor real de la placa
        sqlite3_bind_text(stmt, 1, placa.c_str(), -1, SQLITE_STATIC);

        // Ejecuta la consulta; SQLITE_ROW significa que encontró al menos una fila
        if (sqlite3_step(stmt) == SQLITE_ROW) {

            // Lee cada columna del resultado (índices según el orden del SELECT de arriba)
            string prop  = (const char*)sqlite3_column_text(stmt, 1);  // columna 1: propietario
            string cat   = (const char*)sqlite3_column_text(stmt, 2);  // columna 2: categoría
            double peso  = sqlite3_column_double(stmt, 3);             // columna 3: peso en toneladas
            double saldo = sqlite3_column_double(stmt, 4);             // columna 4: saldo del TAG

            double tarifa  = getTarifa(cat);                       // tarifa base según categoría
            double recargo = esHoraPico() ? tarifa * 0.1 : 0;     // +10% si es hora pico (6-9AM)
            double total   = tarifa + recargo;                     // total que se cobraría por TAG

            bool saldo_ok = saldo >= total; // true si el saldo alcanza para pagar el peaje

            sqlite3_finalize(stmt); // libera la memoria de la consulta preparada

            // Construye el JSON con todos los datos del vehículo para el JS
            string json =
                "{"
                "\"encontrado\":true,"                                            // si está en la BD
                "\"placa\":\""         + placa              + "\","
                "\"propietario\":\""   + prop               + "\","
                "\"categoria\":\""     + cat                + "\","
                "\"peso_ton\":"        + to_string(peso)    + ","
                "\"saldo\":"           + to_string(saldo)   + ","
                "\"tarifa_base\":"     + to_string(tarifa)  + ","
                "\"recargo\":"         + to_string(recargo) + ","
                "\"total\":"           + to_string(total)   + ","
                "\"saldo_suficiente\":" + (saldo_ok      ? "true" : "false") + ","
                "\"horario_pico\":"     + (esHoraPico()  ? "true" : "false") +
                "}";

            return http(json);
        }

        // La placa no existe en la BD: no está registrada con TAG
        sqlite3_finalize(stmt); // siempre liberar memoria, incluso si no hubo resultado
        return http("{\"encontrado\":false}");
    }

    // ── RUTA: POST /api/cobrar-efectivo ──────────────────────────────────────────
    // Registra un cobro en efectivo para un vehículo sin TAG (o con saldo insuficiente).
    // Recibe: placa, categoría, peso y si es residente (es_residente=1).
    // Calcula tarifa + recargo de hora pico + multa por exceso de peso.
    // Guarda el cobro en historial y retorna el JSON para mostrar el recibo.
    if (method == "POST" && path == "/api/cobrar-efectivo") {

        // El cuerpo del POST viene después de la doble línea en blanco (\r\n\r\n)
        // Ejemplo de body: "placa=ABC123&categoria=I&peso=4.5&es_residente=0"
        string body = req.substr(req.find("\r\n\r\n") + 4);

        // Extrae cada campo del formulario
        string placa     = getQueryParam(body, "placa");
        string cat       = getQueryParam(body, "categoria");
        string pesoStr   = getQueryParam(body, "peso");
        string residente = getQueryParam(body, "es_residente"); // "1" si marcó residente en el toggle

        // Convierte la placa a mayúsculas (defensa doble: el JS ya lo hace, pero es más seguro)
        transform(placa.begin(), placa.end(), placa.begin(), ::toupper);

        // Verifica que todos los campos obligatorios llegaron con valor
        if (placa.empty() || cat.empty() || pesoStr.empty()) {
            return http("{\"error\":\"datos incompletos\"}");
        }

        // Valida el formato de la placa antes de continuar
        if (!placaValida(placa)) {
            return http("{\"error\":\"placa invalida\"}");
        }

        // Si el operador marcó que el conductor es residente, se fuerza la categoría IE.
        // Esto ocurre en el servidor (no en el JS) para garantizar que nadie pueda
        // manipular el formulario y obtener la tarifa IE sin ser residente.
        if (residente == "1") cat = "IE";

        double peso    = stod(pesoStr);                        // convierte el texto del peso a número decimal
        double tarifa  = getTarifa(cat);                       // tarifa base según categoría
        double recargo = esHoraPico() ? tarifa * 0.1 : 0;     // +10% si es hora pico (6-9AM)
        double multa   = getMulta(cat, peso);                  // multa si excede límite+1T de su categoría
        double total   = tarifa + recargo + multa;             // total final a cobrar al conductor

        // Inserta el cobro en la tabla historial con todos sus datos.
        // Los valores ya fueron validados arriba, por eso se concatenan directamente.
        string sql =
            "INSERT INTO historial VALUES(NULL,'"
            + placa + "','" + cat + "',"
            + to_string(peso)    + ","  // peso declarado del vehículo
            + to_string(tarifa)  + ","  // tarifa base aplicada
            + to_string(recargo) + ","  // recargo por hora pico (0 si no aplica)
            + to_string(multa)   + ","  // multa por exceso de peso (0 si no aplica)
            + to_string(total)   + ","  // total cobrado
            "'EFECTIVO','" + hora() + "','" + fecha() + "')";

        sqlite3_exec(db, sql.c_str(), 0, 0, 0); // ejecuta el INSERT en la BD

        // Construye el JSON de respuesta para que el JS lo use al mostrar el recibo
        string json =
            "{"
            "\"placa\":\""     + placa             + "\","
            "\"categoria\":\""  + cat              + "\","
            "\"metodo\":\"EFECTIVO\","
            "\"tarifa_base\":"  + to_string(tarifa)  + ","
            "\"recargo\":"      + to_string(recargo) + ","
            "\"multa\":"        + to_string(multa)   + ","
            "\"total\":"        + to_string(total)   + ","
            "\"hora\":\""       + hora()   + "\","
            "\"fecha\":\""      + fecha()  + "\","
            "\"hay_multa\":"    + (multa > 0 ? "true" : "false") + "," // true activa la alerta roja en el recibo
            "\"recibo_id\":1"   // placeholder: en una versión futura retornar el id real del INSERT
            "}";

        return http(json);
    }

    // ── RUTA: POST /api/cobrar-tag ────────────────────────────────────────────────
    // Realiza un cobro descontando directamente del saldo TAG del vehículo en la BD.
    // Pasos: buscar vehículo → verificar saldo → descontar → guardar en historial.
    // Los cobros TAG nunca tienen multa (el peso ya fue verificado al registrar el vehículo).
    if (method == "POST" && path == "/api/cobrar-tag") {

        // Extrae la placa del cuerpo del POST
        string body  = req.substr(req.find("\r\n\r\n") + 4);
        string placa = getQueryParam(body, "placa");

        // Busca el vehículo en la tabla usuarios para obtener sus datos TAG
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db,
            "SELECT cat, peso, saldo FROM usuarios WHERE placa=?",
            -1, &stmt, nullptr);

        sqlite3_bind_text(stmt, 1, placa.c_str(), -1, SQLITE_STATIC);

        // Si la placa no existe en la BD, no se puede cobrar por TAG
        if (sqlite3_step(stmt) != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            return http("{\"error\":\"placa no encontrada\"}");
        }

        // Recupera los datos del vehículo de la consulta
        string cat   = (const char*)sqlite3_column_text(stmt, 0); // columna 0: categoría
        double peso  = sqlite3_column_double(stmt, 1);            // columna 1: peso registrado
        double saldo = sqlite3_column_double(stmt, 2);            // columna 2: saldo actual del TAG

        double tarifa  = getTarifa(cat);                       // tarifa base
        double recargo = esHoraPico() ? tarifa * 0.1 : 0;     // recargo si es hora pico
        double total   = tarifa + recargo;                     // cobros TAG nunca tienen multa

        // Verifica que el saldo TAG sea suficiente para el cobro
        if (saldo < total) {
            sqlite3_finalize(stmt);
            return http("{\"error\":\"saldo insuficiente\"}"); // el JS redirigirá al cobro en efectivo
        }

        double nuevoSaldo = saldo - total; // calcula el saldo que quedará después del descuento

        // Actualiza el saldo del vehículo en la BD (descuenta lo cobrado)
        sqlite3_exec(db,
            ("UPDATE usuarios SET saldo=" + to_string(nuevoSaldo)
             + " WHERE placa='" + placa + "'").c_str(),
            0, 0, 0);

        // Registra el cobro en el historial
        sqlite3_exec(db,
            ("INSERT INTO historial VALUES(NULL,'"
             + placa + "','" + cat + "',"
             + to_string(peso)    + ","
             + to_string(tarifa)  + ","
             + to_string(recargo) + ","
             "0,"                              // multa = 0 siempre para cobros TAG
             + to_string(total)   + ","
             "'TAG','" + hora() + "','" + fecha() + "')").c_str(),
            0, 0, 0);

        sqlite3_finalize(stmt); // libera memoria de la consulta preparada

        // Construye el JSON de respuesta para mostrar el recibo en el navegador
        string json =
            "{"
            "\"placa\":\""      + placa               + "\","
            "\"categoria\":\""  + cat                 + "\","
            "\"metodo\":\"TAG\","
            "\"tarifa_base\":"  + to_string(tarifa)    + ","
            "\"recargo\":"      + to_string(recargo)   + ","
            "\"multa\":0,"                              // siempre 0 en cobros TAG
            "\"total\":"        + to_string(total)     + ","
            "\"saldo_nuevo\":"  + to_string(nuevoSaldo) + "," // saldo restante para mostrarlo en el recibo
            "\"hora\":\""       + hora()   + "\","
            "\"fecha\":\""      + fecha()  + "\","
            "\"hay_multa\":false,"                      // nunca hay multa en cobros TAG
            "\"recibo_id\":1"
            "}";

        return http(json);
    }

    // ── RUTA: GET /api/stats ──────────────────────────────────────────────────────
    // Cuenta los cobros realizados HOY, separados por método (TAG / EFECTIVO).
    // El JS muestra estos números en las tarjetas de resumen de la pantalla de inicio.
    // Esta lógica vive en el servidor para que sea la BD quien filtre, no el navegador.
    if (method == "GET" && path.find("/api/stats") != string::npos) {

        string hoy = fecha(); // fecha actual "YYYY-MM-DD" para filtrar en la BD

        sqlite3_stmt* stmt;
        // Una sola consulta SQL que cuenta total, TAG y EFECTIVO del día simultáneamente.
        // CASE WHEN metodo='TAG' THEN 1 ELSE 0: retorna 1 solo para filas TAG, luego SUM suma esos 1s.
        sqlite3_prepare_v2(db,
            "SELECT "
            "COUNT(*) as total, "
            "SUM(CASE WHEN metodo='TAG'      THEN 1 ELSE 0 END) as tag, "
            "SUM(CASE WHEN metodo='EFECTIVO' THEN 1 ELSE 0 END) as efectivo "
            "FROM historial WHERE fecha=?",  // filtra solo los cobros de hoy
            -1, &stmt, nullptr);

        sqlite3_bind_text(stmt, 1, hoy.c_str(), -1, SQLITE_STATIC); // liga la fecha al "?"

        int total = 0, tag = 0, efectivo = 0; // valores por defecto si no hay registros
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            total    = sqlite3_column_int(stmt, 0); // columna 0: COUNT(*) total del día
            tag      = sqlite3_column_int(stmt, 1); // columna 1: suma de cobros TAG
            efectivo = sqlite3_column_int(stmt, 2); // columna 2: suma de cobros EFECTIVO
        }
        sqlite3_finalize(stmt);

        // Retorna los tres contadores como JSON
        string json =
            "{\"total\":"   + to_string(total)    + ","
            "\"tag\":"      + to_string(tag)       + ","
            "\"efectivo\":" + to_string(efectivo)  + "}";

        return http(json);
    }

    // ── RUTA: GET /api/historial ──────────────────────────────────────────────────
    // Retorna los últimos 50 cobros registrados, del más reciente al más antiguo.
    // El JS los muestra en la tabla de historial con un botón para ver cada recibo.
    if (method == "GET" && path.find("/api/historial") != string::npos) {

        sqlite3_stmt* stmt;
        // ORDER BY id DESC: más reciente primero | LIMIT 50: máximo 50 filas
        sqlite3_prepare_v2(db,
            "SELECT * FROM historial ORDER BY id DESC LIMIT 50",
            -1, &stmt, nullptr);

        string json = "[";  // inicia el array JSON
        bool first  = true; // controla si se pone "," entre objetos (no antes del primero)

        // Itera fila por fila del resultado
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (!first) json += ","; // separador entre objetos JSON del array
            first = false;

            // Construye el objeto JSON para esta fila
            // Los índices (0, 1, 2...) corresponden al orden de columnas en la tabla historial:
            // 0=id, 1=placa, 2=categoria, 3=peso, 4=tarifa, 5=recargo, 6=multa, 7=total, 8=metodo, 9=hora, 10=fecha
            json += "{"
                "\"id\":"          + to_string(sqlite3_column_int(stmt, 0))    + ","
                "\"placa\":\""     + string((const char*)sqlite3_column_text(stmt, 1))  + "\","
                "\"categoria\":\"" + string((const char*)sqlite3_column_text(stmt, 2))  + "\","
                "\"peso\":"        + to_string(sqlite3_column_double(stmt, 3)) + ","
                "\"tarifa\":"      + to_string(sqlite3_column_double(stmt, 4)) + ","
                "\"recargo\":"     + to_string(sqlite3_column_double(stmt, 5)) + ","
                "\"multa\":"       + to_string(sqlite3_column_double(stmt, 6)) + ","
                "\"total\":"       + to_string(sqlite3_column_double(stmt, 7)) + ","
                "\"metodo\":\""    + string((const char*)sqlite3_column_text(stmt, 8))  + "\","
                "\"hora\":\""      + string((const char*)sqlite3_column_text(stmt, 9))  + "\","
                "\"fecha\":\""     + string((const char*)sqlite3_column_text(stmt, 10)) + "\""
                "}";
        }

        json += "]"; // cierra el array JSON
        sqlite3_finalize(stmt);

        return http(json);
    }

    // ── RUTA NO ENCONTRADA ────────────────────────────────────────────────────────
    // Si ninguna ruta coincidió, retorna un mensaje de error genérico
    return http("{\"error\":\"ruta no encontrada\"}");
}


// ============================================================
//  SECCIÓN: MAIN
//  Punto de entrada del programa.
//  Inicializa la red y la BD, luego entra en un bucle
//  infinito atendiendo una conexión a la vez.
// ============================================================

int main() {

    // Inicializa la librería de sockets de Windows (Winsock).
    // MAKEWORD(2,2) indica que queremos la versión 2.2 del protocolo de red.
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    // Crea las tablas en la BD (si aún no existen)
    initDB();

    // Crea un socket TCP (SOCK_STREAM = orientado a conexión) sobre IPv4 (AF_INET)
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);

    // Configura la dirección en la que escuchará el servidor:
    sockaddr_in addr;
    addr.sin_family      = AF_INET;    // familia IPv4
    addr.sin_port        = htons(8080); // puerto 8080 en formato de red (htons corrige byte order)
    addr.sin_addr.s_addr = INADDR_ANY; // acepta conexiones desde cualquier interfaz local

    bind(s, (sockaddr*)&addr, sizeof(addr)); // asocia el socket a la dirección/puerto configurados
    listen(s, 5);                            // pone el socket en modo escucha (cola de hasta 5)

    cout << "Servidor activo en http://localhost:8080\n";

    // Bucle principal: el servidor nunca termina, atiende conexiones indefinidamente.
    // Atiende UN cliente a la vez (servidor bloqueante, sin hilos).
    while (true) {

        SOCKET c = accept(s, 0, 0); // bloquea aquí hasta que llegue una conexión del navegador

        char buf[8192];                        // buffer donde se guardará la petición HTTP recibida
        int n = recv(c, buf, sizeof(buf), 0);  // lee los bytes enviados por el navegador

        string req(buf, n);        // convierte el buffer de caracteres en string de C++
        string res = router(req);  // pasa la petición al router para obtener la respuesta correcta

        send(c, res.c_str(), static_cast<int>(res.size()), 0); // envía la respuesta HTTP al navegador
        closesocket(c); // cierra esta conexión (HTTP sin keep-alive: una petición por conexión)
    }

    return 0; // nunca se llega aquí en ejecución normal, pero es buena práctica incluirlo
}