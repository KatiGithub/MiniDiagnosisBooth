const express = require('express');
const bodyParser= require('body-parser');
const app = express();
var axios = require('axios');
var fs = require('fs');

app.use(bodyParser.json({extended : true}));

var bodyString;
var student_id;
var student_id_string;
let symptom_id;

let preliminary_question_result;

app.post('/', (req, res) => {
    bodyString = req.body;
    var today = new Date();
    var time = today.getHours() + ":" + today.getMinutes() + ":" + today.getSeconds();
    console.log(time + " Got Body: ", bodyString);    
    student_id = bodyString.student_id;
    console.log(student_id);
    student_id_string = student_id.toString();

    studentTemp = bodyString.temp;
    heartrate = bodyString.heartrate;
    oxygenpercent = bodyString.percent;

    try {
        if(fs.existsSync(student_id_string)) {
            fs.appendFile(student_id_string, "\n----------------------------- " + student_id_string + " " + getDateandTime() + "\nTemperature: " + studentTemp + "\nOxygenPercent: " + oxygenpercent + "\nheartrate: " + heartrate, (err) =>  {
                if(err) throw err;
                console.log("Appended new log to file");
            });
        } else {
            fs.appendFile(student_id_string, student_id_string + "\n----------------------------- " + getDateandTime()  + "\nTemperature: " + studentTemp + "\nOxygenPercent: " + oxygenpercent + "\nheartrate: " + heartrate, (err) =>  {
                if(err) throw err;
                console.log("Created and appended new log to file");
            });
        }
    } catch (err) {
        console.error(err);
    }    
    res.status(200).send("preliminary-questions");
});

app.post('/preliminary-question', (req, res) => {
    bodyString = req.body;

    if(student_id_string == null) {
        res.status(200).send("student-id-missing");
    } else {
        for(const [key, value] of Object.entries(bodyString)) {

            if(key.toString() != "symptoms" || key.toString() != "abnormalities") {
                fs.appendFile(student_id_string, "\n" + key.toString() + ": " + value.toString(), (err) => {
                    console.log("Added: " + key.toString() + ": " + value.toString());
                });
            } else {
                fs.appendFile(student_id_string, "\n" + key.toString() + ": ", (err) => {
                    if(err) console.error(err);
                });
                for(const symptoms_abnormalities of value) {
                    fs.appendFile(student_id_string, "\n    " + symptoms_abnormalities.toString(), (err) => {
                        console.log("Added: " + symptoms_abnormalities.toString());
                    })
                }
            }
        }
    }

    var querystring = createJsonStage1(bodyString);
    console.log(querystring);
    let responsebody_object

    sendToApi(querystring).then((result) => {
        console.log(result);

        let question = result.question.text;
        fs.appendFile(student_id_string, "\n" + question + ": ", (err) => {
            if(err) throw err;
        });

        let list = result.question.items;
        symptom_id = list[0].id;

        let response = {question: question};

        res.status(200).send(response);
    });
});

app.post('/api-middleman', (req, res) => {
    let body = req.body;

    fs.appendFile(student_id_string, body.choice, (err) => {
        if(err) throw (err);
    })

    symptoms = preliminary_question_result.evidence;

    symptoms.push({id: symptom_id, choice_id: body.choice, source: "initial"});
    preliminary_question_result.evidence = symptoms;

    console.log(preliminary_question_result);

    sendToApi(JSON.stringify(preliminary_question_result)).then((result) => {

        if(result.should_stop) {
            res.status(200).send("interview-over");
        } else {
            console.log(result);

            let list = result.question.items;
            symptom_id = list[0].id;
            let question = result.question.text;
            fs.appendFile(student_id_string, "\n" + question + ": ", (err) => {
                if(err) throw err;
            })
            let response = {question: question};

            res.status(200).send(response);
        }
    })
});

app.post('/interview-over', (req, res) => {
    fs.appendFile(student_id_string, "\nConditions: ", (err) => {
        if(err) throw err;
    });

    let conditions_object;

    sendToApi(JSON.stringify(preliminary_question_result)).then((result) => {
        let conditions = result.conditions;

        let conditions_object;
        conditions.forEach((item, index) => {
            conditions_object[item.common_name] = item.probablity * 100;
        })
    });

    res.status(200).send(conditions_object);
}) 

function getDateandTime() {
    var today = new Date();
    var date = today.getFullYear()+'-'+(today.getMonth()+1)+'-'+today.getDate() + "--" + today.getHours() + ":" + today.getMinutes();
    return date;
}
    

function createJsonStage1(object) {
    var gender;
    var age = object.age;
    var smoke = object.smoke;
    // var allergies;
    var symptoms = [];
    
    if(object.gender == "M") {
        gender = "male";
    } else {
        gender = "female";
    }

    for(const i_symptoms of object.symptoms) {
        switch(i_symptoms) {
            case "fatigue":
                symptoms.push({id: "s_2100", choice_id: "present", source: "initial"});
            case "breathing-problem":
                symptoms.push({id: "s_2076", choice_id: "present", source: "initial"});
            case "chest-pain":
                symptoms.push({id: "s_50", choice_id: "present", source: "initial"});
            case "rashes":
                symptoms.push({id: "s_417", choice_id: "present", source: "initial"});
            case "diarrhea":
                symptoms.push({id: "s_8", choice_id: "present", source: "initial"});
            case "fever":
                symptoms.push({id: "s_98", choice_id: "present", source: "initial"});
            case "headache":
                symptoms.push({id: "s_21", choice_id: "present", source: "initial"});
            case "stomach-ache":
                symptoms.push({id: "s_13", choice_id: "present", source: "initial"});
        }
    }

    var jsonObjectReturn = {
        sex: gender,
        age: age,
        evidence: symptoms,
        extras: {
            disable_groups: true
        }
    }

    var jsonObjectString = JSON.stringify(jsonObjectReturn);
    preliminary_question_result = jsonObjectReturn;
    return jsonObjectString;
}

async function sendToApi(querystring) {

    var config = {
        method: 'post',
        url: 'https://api.infermedica.com/v2/diagnosis',
        headers: { 
          'App-Key': 'd2ff68c12d681a03dbbf24459d545487', 
          'App-Id': '4aa3a9d4', 
          'Content-Type': 'application/json'
        },
        data : querystring
      };

    let response = axios(config);
    
    const dataResponse = response.then((response) => response.data);

    return dataResponse;
}

app.listen(3000, () => console.log('Server on http://localhost:3000'));